/*
 * SSL/TLS interface definition
 * Copyright (c) 2004-2013, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef TLS_H
#define TLS_H

struct tls_connection;

struct tls_random {
	const u8 *client_random;
	size_t client_random_len;
	const u8 *server_random;
	size_t server_random_len;
};

enum tls_event {
	TLS_CERT_CHAIN_SUCCESS,
	TLS_CERT_CHAIN_FAILURE,
	TLS_PEER_CERTIFICATE,
	TLS_ALERT
};

/*
 * Note: These are used as identifier with external programs and as such, the
 * values must not be changed.
 */
enum tls_fail_reason {
	TLS_FAIL_UNSPECIFIED = 0,
	TLS_FAIL_UNTRUSTED = 1,
	TLS_FAIL_REVOKED = 2,
	TLS_FAIL_NOT_YET_VALID = 3,
	TLS_FAIL_EXPIRED = 4,
	TLS_FAIL_SUBJECT_MISMATCH = 5,
	TLS_FAIL_ALTSUBJECT_MISMATCH = 6,
	TLS_FAIL_BAD_CERTIFICATE = 7,
	TLS_FAIL_SERVER_CHAIN_PROBE = 8,
	TLS_FAIL_DOMAIN_SUFFIX_MISMATCH = 9,
	TLS_FAIL_DOMAIN_MISMATCH = 10,
	TLS_FAIL_INSUFFICIENT_KEY_LEN = 11,
};


#define TLS_MAX_ALT_SUBJECT 10

union tls_event_data {
	struct {
		int depth;
		const char *subject;
		enum tls_fail_reason reason;
		const char *reason_txt;
		const struct wpabuf *cert;
	} cert_fail;

	struct {
		int depth;
		const char *subject;
		const struct wpabuf *cert;
		const u8 *hash;
		size_t hash_len;
		const char *altsubject[TLS_MAX_ALT_SUBJECT];
		int num_altsubject;
		const char *serial_num;
	} peer_cert;

	struct {
		int is_local;
		const char *type;
		const char *description;
	} alert;
};

struct tls_config {
	const char *opensc_engine_path;
	const char *pkcs11_engine_path;
	const char *pkcs11_module_path;
	int fips_mode;
	int cert_in_cb;
	const char *openssl_ciphers;
	unsigned int tls_session_lifetime;
	unsigned int tls_flags;

	void (*event_cb)(void *ctx, enum tls_event ev,
			 union tls_event_data *data);
	void *cb_ctx;
};

#define TLS_CONN_ALLOW_SIGN_RSA_MD5 BIT(0)
#define TLS_CONN_DISABLE_TIME_CHECKS BIT(1)
#define TLS_CONN_DISABLE_SESSION_TICKET BIT(2)
#define TLS_CONN_REQUEST_OCSP BIT(3)
#define TLS_CONN_REQUIRE_OCSP BIT(4)
#define TLS_CONN_DISABLE_TLSv1_1 BIT(5)
#define TLS_CONN_DISABLE_TLSv1_2 BIT(6)
#define TLS_CONN_EAP_FAST BIT(7)
#define TLS_CONN_DISABLE_TLSv1_0 BIT(8)
#define TLS_CONN_EXT_CERT_CHECK BIT(9)
#define TLS_CONN_REQUIRE_OCSP_ALL BIT(10)
#define TLS_CONN_SUITEB BIT(11)
#define TLS_CONN_SUITEB_NO_ECDH BIT(12)
#define TLS_CONN_DISABLE_TLSv1_3 BIT(13)

/**
 * struct tls_connection_params - Parameters for TLS connection
 * @ca_cert: File or reference name for CA X.509 certificate in PEM or DER
 * format
 * @ca_cert_blob: ca_cert as inlined data or %NULL if not used
 * @ca_cert_blob_len: ca_cert_blob length
 * @ca_path: Path to CA certificates (OpenSSL specific)
 * @subject_match: String to match in the subject of the peer certificate or
 * %NULL to allow all subjects
 * @altsubject_match: String to match in the alternative subject of the peer
 * certificate or %NULL to allow all alternative subjects
 * @suffix_match: String to suffix match in the dNSName or CN of the peer
 * certificate or %NULL to allow all domain names. This may allow subdomains an
 * wildcard certificates. Each domain name label must have a full match.
 * @domain_match: String to match in the dNSName or CN of the peer
 * certificate or %NULL to allow all domain names. This requires a full,
 * case-insensitive match.
 * @client_cert: File or reference name for client X.509 certificate in PEM or
 * DER format
 * @client_cert_blob: client_cert as inlined data or %NULL if not used
 * @client_cert_blob_len: client_cert_blob length
 * @private_key: File or reference name for client private key in PEM or DER
 * format (traditional format (RSA PRIVATE KEY) or PKCS#8 (PRIVATE KEY)
 * @private_key_blob: private_key as inlined data or %NULL if not used
 * @private_key_blob_len: private_key_blob length
 * @private_key_passwd: Passphrase for decrypted private key, %NULL if no
 * passphrase is used.
 * @dh_file: File name for DH/DSA data in PEM format, or %NULL if not used
 * @dh_blob: dh_file as inlined data or %NULL if not used
 * @dh_blob_len: dh_blob length
 * @engine: 1 = use engine (e.g., a smartcard) for private key operations
 * (this is OpenSSL specific for now)
 * @engine_id: engine id string (this is OpenSSL specific for now)
 * @ppin: pointer to the pin variable in the configuration
 * (this is OpenSSL specific for now)
 * @key_id: the private key's id when using engine (this is OpenSSL
 * specific for now)
 * @cert_id: the certificate's id when using engine
 * @ca_cert_id: the CA certificate's id when using engine
 * @openssl_ciphers: OpenSSL cipher configuration
 * @flags: Parameter options (TLS_CONN_*)
 * @ocsp_stapling_response: DER encoded file with cached OCSP stapling response
 *	or %NULL if OCSP is not enabled
 * @ocsp_stapling_response_multi: DER encoded file with cached OCSP stapling
 *	response list (OCSPResponseList for ocsp_multi in RFC 6961) or %NULL if
 *	ocsp_multi is not enabled
 *
 * TLS connection parameters to be configured with tls_connection_set_params()
 * and tls_global_set_params().
 *
 * Certificates and private key can be configured either as a reference name
 * (file path or reference to certificate store) or by providing the same data
 * as a pointer to the data in memory. Only one option will be used for each
 * field.
 */
struct tls_connection_params {
	const char *ca_cert;
	const u8 *ca_cert_blob;
	size_t ca_cert_blob_len;
	const char *ca_path;
	const char *subject_match;
	const char *altsubject_match;
	const char *suffix_match;
	const char *domain_match;
	const char *client_cert;
	const u8 *client_cert_blob;
	size_t client_cert_blob_len;
	const char *private_key;
	const u8 *private_key_blob;
	size_t private_key_blob_len;
	const char *private_key_passwd;
	const char *dh_file;
	const u8 *dh_blob;
	size_t dh_blob_len;

	/* OpenSSL specific variables */
	int engine;
	const char *engine_id;
	const char *pin;
	const char *key_id;
	const char *cert_id;
	const char *ca_cert_id;
	const char *openssl_ciphers;

	unsigned int flags;
	const char *ocsp_stapling_response;
	const char *ocsp_stapling_response_multi;
};


/**
 * tls_init - Initialize TLS library
 * @conf: Configuration data for TLS library
 * Returns: Context data to be used as tls_ctx in calls to other functions,
 * or %NULL on failure.
 *
 * Called once during program startup and once for each RSN pre-authentication
 * session. In other words, there can be two concurrent TLS contexts. If global
 * library initialization is needed (i.e., one that is shared between both
 * authentication types), the TLS library wrapper should maintain a reference
 * counter and do global initialization only when moving from 0 to 1 reference.
 */
void * tls_init(const struct tls_config *conf);

/**
 * tls_deinit - Deinitialize TLS library
 * @tls_ctx: TLS context data from tls_init()
 *
 * Called once during program shutdown and once for each RSN pre-authentication
 * session. If global library deinitialization is needed (i.e., one that is
 * shared between both authentication types), the TLS library wrapper should
 * maintain a reference counter and do global deinitialization only when moving
 * from 1 to 0 references.
 */
void tls_deinit(void *tls_ctx);

/**
 * tls_get_errors - Process pending errors
 * @tls_ctx: TLS context data from tls_init()
 * Returns: Number of found error, 0 if no errors detected.
 *
 * Process all pending TLS errors.
 */
int tls_get_errors(void *tls_ctx);

/**
 * tls_connection_init - Initialize a new TLS connection
 * @tls_ctx: TLS context data from tls_init()
 * Returns: Connection context data, conn for other function calls
 */
struct tls_connection * tls_connection_init(void *tls_ctx);

/**
 * tls_connection_deinit - Free TLS connection data
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 *
 * Release all resources allocated for TLS connection.
 */
void tls_connection_deinit(void *tls_ctx, struct tls_connection *conn);

/**
 * tls_connection_established - Has the TLS connection been completed?
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * Returns: 1 if TLS connection has been completed, 0 if not.
 */
int tls_connection_established(void *tls_ctx, struct tls_connection *conn);

/**
 * tls_connection_peer_serial_num - Fetch peer certificate serial number
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * Returns: Allocated string buffer containing the peer certificate serial
 * number or %NULL on error.
 *
 * The caller is responsible for freeing the returned buffer with os_free().
 */
char * tls_connection_peer_serial_num(void *tls_ctx,
				      struct tls_connection *conn);

/**
 * tls_connection_shutdown - Shutdown TLS connection
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * Returns: 0 on success, -1 on failure
 *
 * Shutdown current TLS connection without releasing all resources. New
 * connection can be started by using the same conn without having to call
 * tls_connection_init() or setting certificates etc. again. The new
 * connection should try to use session resumption.
 */
int tls_connection_shutdown(void *tls_ctx, struct tls_connection *conn);

enum {
	TLS_SET_PARAMS_ENGINE_PRV_BAD_PIN = -4,
	TLS_SET_PARAMS_ENGINE_PRV_VERIFY_FAILED = -3,
	TLS_SET_PARAMS_ENGINE_PRV_INIT_FAILED = -2
};

/**
 * tls_connection_set_params - Set TLS connection parameters
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * @params: Connection parameters
 * Returns: 0 on success, -1 on failure,
 * TLS_SET_PARAMS_ENGINE_PRV_INIT_FAILED (-2) on error causing PKCS#11 engine
 * failure, or
 * TLS_SET_PARAMS_ENGINE_PRV_VERIFY_FAILED (-3) on failure to verify the
 * PKCS#11 engine private key, or
 * TLS_SET_PARAMS_ENGINE_PRV_BAD_PIN (-4) on PIN error causing PKCS#11 engine
 * failure.
 */
int __must_check
tls_connection_set_params(void *tls_ctx, struct tls_connection *conn,
			  const struct tls_connection_params *params);

/**
 * tls_global_set_params - Set TLS parameters for all TLS connection
 * @tls_ctx: TLS context data from tls_init()
 * @params: Global TLS parameters
 * Returns: 0 on success, -1 on failure,
 * TLS_SET_PARAMS_ENGINE_PRV_INIT_FAILED (-2) on error causing PKCS#11 engine
 * failure, or
 * TLS_SET_PARAMS_ENGINE_PRV_VERIFY_FAILED (-3) on failure to verify the
 * PKCS#11 engine private key, or
 * TLS_SET_PARAMS_ENGINE_PRV_BAD_PIN (-4) on PIN error causing PKCS#11 engine
 * failure.
 */
int __must_check tls_global_set_params(
	void *tls_ctx, const struct tls_connection_params *params);

/**
 * tls_global_set_verify - Set global certificate verification options
 * @tls_ctx: TLS context data from tls_init()
 * @check_crl: 0 = do not verify CRLs, 1 = verify CRL for the user certificate,
 * 2 = verify CRL for all certificates
 * Returns: 0 on success, -1 on failure
 */
int __must_check tls_global_set_verify(void *tls_ctx, int check_crl);

/**
 * tls_connection_set_verify - Set certificate verification options
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * @verify_peer: 1 = verify peer certificate
 * @flags: Connection flags (TLS_CONN_*)
 * @session_ctx: Session caching context or %NULL to use default
 * @session_ctx_len: Length of @session_ctx in bytes.
 * Returns: 0 on success, -1 on failure
 */
int __must_check tls_connection_set_verify(void *tls_ctx,
					   struct tls_connection *conn,
					   int verify_peer,
					   unsigned int flags,
					   const u8 *session_ctx,
					   size_t session_ctx_len);

/**
 * tls_connection_get_random - Get random data from TLS connection
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * @data: Structure of client/server random data (filled on success)
 * Returns: 0 on success, -1 on failure
 */
int __must_check tls_connection_get_random(void *tls_ctx,
					 struct tls_connection *conn,
					 struct tls_random *data);

/**
 * tls_connection_export_key - Derive keying material from a TLS connection
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * @label: Label (e.g., description of the key) for PRF
 * @out: Buffer for output data from TLS-PRF
 * @out_len: Length of the output buffer
 * Returns: 0 on success, -1 on failure
 *
 * Exports keying material using the mechanism described in RFC 5705.
 */
int __must_check tls_connection_export_key(void *tls_ctx,
					   struct tls_connection *conn,
					   const char *label,
					   u8 *out, size_t out_len);

/**
 * tls_connection_get_eap_fast_key - Derive key material for EAP-FAST
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * @out: Buffer for output data from TLS-PRF
 * @out_len: Length of the output buffer
 * Returns: 0 on success, -1 on failure
 *
 * Exports key material after the normal TLS key block for use with
 * EAP-FAST. Most callers will want tls_connection_export_key(), but EAP-FAST
 * uses a different legacy mechanism.
 */
int __must_check tls_connection_get_eap_fast_key(void *tls_ctx,
						 struct tls_connection *conn,
						 u8 *out, size_t out_len);

/**
 * tls_connection_handshake - Process TLS handshake (client side)
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * @in_data: Input data from TLS server
 * @appl_data: Pointer to application data pointer, or %NULL if dropped
 * Returns: Output data, %NULL on failure
 *
 * The caller is responsible for freeing the returned output data. If the final
 * handshake message includes application data, this is decrypted and
 * appl_data (if not %NULL) is set to point this data. The caller is
 * responsible for freeing appl_data.
 *
 * This function is used during TLS handshake. The first call is done with
 * in_data == %NULL and the library is expected to return ClientHello packet.
 * This packet is then send to the server and a response from server is given
 * to TLS library by calling this function again with in_data pointing to the
 * TLS message from the server.
 *
 * If the TLS handshake fails, this function may return %NULL. However, if the
 * TLS library has a TLS alert to send out, that should be returned as the
 * output data. In this case, tls_connection_get_failed() must return failure
 * (> 0).
 *
 * tls_connection_established() should return 1 once the TLS handshake has been
 * completed successfully.
 */
struct wpabuf * tls_connection_handshake(void *tls_ctx,
					 struct tls_connection *conn,
					 const struct wpabuf *in_data,
					 struct wpabuf **appl_data);

struct wpabuf * tls_connection_handshake2(void *tls_ctx,
					  struct tls_connection *conn,
					  const struct wpabuf *in_data,
					  struct wpabuf **appl_data,
					  int *more_data_needed);

/**
 * tls_connection_server_handshake - Process TLS handshake (server side)
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * @in_data: Input data from TLS peer
 * @appl_data: Pointer to application data pointer, or %NULL if dropped
 * Returns: Output data, %NULL on failure
 *
 * The caller is responsible for freeing the returned output data.
 */
struct wpabuf * tls_connection_server_handshake(void *tls_ctx,
						struct tls_connection *conn,
						const struct wpabuf *in_data,
						struct wpabuf **appl_data);

/**
 * tls_connection_encrypt - Encrypt data into TLS tunnel
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * @in_data: Plaintext data to be encrypted
 * Returns: Encrypted TLS data or %NULL on failure
 *
 * This function is used after TLS handshake has been completed successfully to
 * send data in the encrypted tunnel. The caller is responsible for freeing the
 * returned output data.
 */
struct wpabuf * tls_connection_encrypt(void *tls_ctx,
				       struct tls_connection *conn,
				       const struct wpabuf *in_data);

/**
 * tls_connection_decrypt - Decrypt data from TLS tunnel
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * @in_data: Encrypted TLS data
 * Returns: Decrypted TLS data or %NULL on failure
 *
 * This function is used after TLS handshake has been completed successfully to
 * receive data from the encrypted tunnel. The caller is responsible for
 * freeing the returned output data.
 */
struct wpabuf * tls_connection_decrypt(void *tls_ctx,
				       struct tls_connection *conn,
				       const struct wpabuf *in_data);

struct wpabuf * tls_connection_decrypt2(void *tls_ctx,
					struct tls_connection *conn,
					const struct wpabuf *in_data,
					int *more_data_needed);

/**
 * tls_connection_resumed - Was session resumption used
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * Returns: 1 if current session used session resumption, 0 if not
 */
int tls_connection_resumed(void *tls_ctx, struct tls_connection *conn);

enum {
	TLS_CIPHER_NONE,
	TLS_CIPHER_RC4_SHA /* 0x0005 */,
	TLS_CIPHER_AES128_SHA /* 0x002f */,
	TLS_CIPHER_RSA_DHE_AES128_SHA /* 0x0031 */,
	TLS_CIPHER_ANON_DH_AES128_SHA /* 0x0034 */,
	TLS_CIPHER_RSA_DHE_AES256_SHA /* 0x0039 */,
	TLS_CIPHER_AES256_SHA /* 0x0035 */,
};

/**
 * tls_connection_set_cipher_list - Configure acceptable cipher suites
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * @ciphers: Zero (TLS_CIPHER_NONE) terminated list of allowed ciphers
 * (TLS_CIPHER_*).
 * Returns: 0 on success, -1 on failure
 */
int __must_check tls_connection_set_cipher_list(void *tls_ctx,
						struct tls_connection *conn,
						u8 *ciphers);

/**
 * tls_get_version - Get the current TLS version number
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * @buf: Buffer for returning the TLS version number
 * @buflen: buf size
 * Returns: 0 on success, -1 on failure
 *
 * Get the currently used TLS version number.
 */
int __must_check tls_get_version(void *tls_ctx, struct tls_connection *conn,
				 char *buf, size_t buflen);

/**
 * tls_get_cipher - Get current cipher name
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * @buf: Buffer for the cipher name
 * @buflen: buf size
 * Returns: 0 on success, -1 on failure
 *
 * Get the name of the currently used cipher.
 */
int __must_check tls_get_cipher(void *tls_ctx, struct tls_connection *conn,
				char *buf, size_t buflen);

/**
 * tls_connection_enable_workaround - Enable TLS workaround options
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * Returns: 0 on success, -1 on failure
 *
 * This function is used to enable connection-specific workaround options for
 * buffer SSL/TLS implementations.
 */
int __must_check tls_connection_enable_workaround(void *tls_ctx,
						  struct tls_connection *conn);

/**
 * tls_connection_client_hello_ext - Set TLS extension for ClientHello
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * @ext_type: Extension type
 * @data: Extension payload (%NULL to remove extension)
 * @data_len: Extension payload length
 * Returns: 0 on success, -1 on failure
 */
int __must_check tls_connection_client_hello_ext(void *tls_ctx,
						 struct tls_connection *conn,
						 int ext_type, const u8 *data,
						 size_t data_len);

/**
 * tls_connection_get_failed - Get connection failure status
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 *
 * Returns >0 if connection has failed, 0 if not.
 */
int tls_connection_get_failed(void *tls_ctx, struct tls_connection *conn);

/**
 * tls_connection_get_read_alerts - Get connection read alert status
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * Returns: Number of times a fatal read (remote end reported error) has
 * happened during this connection.
 */
int tls_connection_get_read_alerts(void *tls_ctx, struct tls_connection *conn);

/**
 * tls_connection_get_write_alerts - Get connection write alert status
 * @tls_ctx: TLS context data from tls_init()
 * @conn: Connection context data from tls_connection_init()
 * Returns: Number of times a fatal write (locally detected error) has happened
 * during this connection.
 */
int tls_connection_get_write_alerts(void *tls_ctx,
				    struct tls_connection *conn);

typedef int (*tls_session_ticket_cb)
(void *ctx, const u8 *ticket, size_t len, const u8 *client_random,
 const u8 *server_random, u8 *master_secret);

int __must_check  tls_connection_set_session_ticket_cb(
	void *tls_ctx, struct tls_connection *conn,
	tls_session_ticket_cb cb, void *ctx);

void tls_connection_set_log_cb(struct tls_connection *conn,
			       void (*log_cb)(void *ctx, const char *msg),
			       void *ctx);

#define TLS_BREAK_VERIFY_DATA BIT(0)
#define TLS_BREAK_SRV_KEY_X_HASH BIT(1)
#define TLS_BREAK_SRV_KEY_X_SIGNATURE BIT(2)
#define TLS_DHE_PRIME_511B BIT(3)
#define TLS_DHE_PRIME_767B BIT(4)
#define TLS_DHE_PRIME_15 BIT(5)
#define TLS_DHE_PRIME_58B BIT(6)
#define TLS_DHE_NON_PRIME BIT(7)

void tls_connection_set_test_flags(struct tls_connection *conn, u32 flags);

int tls_get_library_version(char *buf, size_t buf_len);

void tls_connection_set_success_data(struct tls_connection *conn,
				     struct wpabuf *data);

void tls_connection_set_success_data_resumed(struct tls_connection *conn);

const struct wpabuf *
tls_connection_get_success_data(struct tls_connection *conn);

void tls_connection_remove_session(struct tls_connection *conn);

#endif /* TLS_H */
