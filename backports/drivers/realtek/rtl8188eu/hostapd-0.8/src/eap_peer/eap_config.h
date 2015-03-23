/*
 * EAP peer configuration data
 * Copyright (c) 2003-2008, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef EAP_CONFIG_H
#define EAP_CONFIG_H

/**
 * struct eap_peer_config - EAP peer configuration/credentials
 */
struct eap_peer_config {
	/**
	 * identity - EAP Identity
	 *
	 * This field is used to set the real user identity or NAI (for
	 * EAP-PSK/PAX/SAKE/GPSK).
	 */
	u8 *identity;

	/**
	 * identity_len - EAP Identity length
	 */
	size_t identity_len;

	/**
	 * anonymous_identity -  Anonymous EAP Identity
	 *
	 * This field is used for unencrypted use with EAP types that support
	 * different tunnelled identity, e.g., EAP-TTLS, in order to reveal the
	 * real identity (identity field) only to the authentication server.
	 *
	 * If not set, the identity field will be used for both unencrypted and
	 * protected fields.
	 */
	u8 *anonymous_identity;

	/**
	 * anonymous_identity_len - Length of anonymous_identity
	 */
	size_t anonymous_identity_len;

	/**
	 * password - Password string for EAP
	 *
	 * This field can include either the plaintext password (default
	 * option) or a NtPasswordHash (16-byte MD4 hash of the unicode
	 * presentation of the password) if flags field has
	 * EAP_CONFIG_FLAGS_PASSWORD_NTHASH bit set to 1. NtPasswordHash can
	 * only be used with authentication mechanism that use this hash as the
	 * starting point for operation: MSCHAP and MSCHAPv2 (EAP-MSCHAPv2,
	 * EAP-TTLS/MSCHAPv2, EAP-TTLS/MSCHAP, LEAP).
	 *
	 * In addition, this field is used to configure a pre-shared key for
	 * EAP-PSK/PAX/SAKE/GPSK. The length of the PSK must be 16 for EAP-PSK
	 * and EAP-PAX and 32 for EAP-SAKE. EAP-GPSK can use a variable length
	 * PSK.
	 */
	u8 *password;

	/**
	 * password_len - Length of password field
	 */
	size_t password_len;

	/**
	 * ca_cert - File path to CA certificate file (PEM/DER)
	 *
	 * This file can have one or more trusted CA certificates. If ca_cert
	 * and ca_path are not included, server certificate will not be
	 * verified. This is insecure and a trusted CA certificate should
	 * always be configured when using EAP-TLS/TTLS/PEAP. Full path to the
	 * file should be used since working directory may change when
	 * wpa_supplicant is run in the background.
	 *
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://blob_name.
	 *
	 * Alternatively, this can be used to only perform matching of the
	 * server certificate (SHA-256 hash of the DER encoded X.509
	 * certificate). In this case, the possible CA certificates in the
	 * server certificate chain are ignored and only the server certificate
	 * is verified. This is configured with the following format:
	 * hash:://server/sha256/cert_hash_in_hex
	 * For example: "hash://server/sha256/
	 * 5a1bc1296205e6fdbe3979728efe3920798885c1c4590b5f90f43222d239ca6a"
	 *
	 * On Windows, trusted CA certificates can be loaded from the system
	 * certificate store by setting this to cert_store://name, e.g.,
	 * ca_cert="cert_store://CA" or ca_cert="cert_store://ROOT".
	 * Note that when running wpa_supplicant as an application, the user
	 * certificate store (My user account) is used, whereas computer store
	 * (Computer account) is used when running wpasvc as a service.
	 */
	u8 *ca_cert;

	/**
	 * ca_path - Directory path for CA certificate files (PEM)
	 *
	 * This path may contain multiple CA certificates in OpenSSL format.
	 * Common use for this is to point to system trusted CA list which is
	 * often installed into directory like /etc/ssl/certs. If configured,
	 * these certificates are added to the list of trusted CAs. ca_cert
	 * may also be included in that case, but it is not required.
	 */
	u8 *ca_path;

	/**
	 * client_cert - File path to client certificate file (PEM/DER)
	 *
	 * This field is used with EAP method that use TLS authentication.
	 * Usually, this is only configured for EAP-TLS, even though this could
	 * in theory be used with EAP-TTLS and EAP-PEAP, too. Full path to the
	 * file should be used since working directory may change when
	 * wpa_supplicant is run in the background.
	 *
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://blob_name.
	 */
	u8 *client_cert;

	/**
	 * private_key - File path to client private key file (PEM/DER/PFX)
	 *
	 * When PKCS#12/PFX file (.p12/.pfx) is used, client_cert should be
	 * commented out. Both the private key and certificate will be read
	 * from the PKCS#12 file in this case. Full path to the file should be
	 * used since working directory may change when wpa_supplicant is run
	 * in the background.
	 *
	 * Windows certificate store can be used by leaving client_cert out and
	 * configuring private_key in one of the following formats:
	 *
	 * cert://substring_to_match
	 *
	 * hash://certificate_thumbprint_in_hex
	 *
	 * For example: private_key="hash://63093aa9c47f56ae88334c7b65a4"
	 *
	 * Note that when running wpa_supplicant as an application, the user
	 * certificate store (My user account) is used, whereas computer store
	 * (Computer account) is used when running wpasvc as a service.
	 *
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://blob_name.
	 */
	u8 *private_key;

	/**
	 * private_key_passwd - Password for private key file
	 *
	 * If left out, this will be asked through control interface.
	 */
	u8 *private_key_passwd;

	/**
	 * dh_file - File path to DH/DSA parameters file (in PEM format)
	 *
	 * This is an optional configuration file for setting parameters for an
	 * ephemeral DH key exchange. In most cases, the default RSA
	 * authentication does not use this configuration. However, it is
	 * possible setup RSA to use ephemeral DH key exchange. In addition,
	 * ciphers with DSA keys always use ephemeral DH keys. This can be used
	 * to achieve forward secrecy. If the file is in DSA parameters format,
	 * it will be automatically converted into DH params. Full path to the
	 * file should be used since working directory may change when
	 * wpa_supplicant is run in the background.
	 *
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://blob_name.
	 */
	u8 *dh_file;

	/**
	 * subject_match - Constraint for server certificate subject
	 *
	 * This substring is matched against the subject of the authentication
	 * server certificate. If this string is set, the server sertificate is
	 * only accepted if it contains this string in the subject. The subject
	 * string is in following format:
	 *
	 * /C=US/ST=CA/L=San Francisco/CN=Test AS/emailAddress=as@n.example.com
	 */
	u8 *subject_match;

	/**
	 * altsubject_match - Constraint for server certificate alt. subject
	 *
	 * Semicolon separated string of entries to be matched against the
	 * alternative subject name of the authentication server certificate.
	 * If this string is set, the server sertificate is only accepted if it
	 * contains one of the entries in an alternative subject name
	 * extension.
	 *
	 * altSubjectName string is in following format: TYPE:VALUE
	 *
	 * Example: EMAIL:server@example.com
	 * Example: DNS:server.example.com;DNS:server2.example.com
	 *
	 * Following types are supported: EMAIL, DNS, URI
	 */
	u8 *altsubject_match;

	/**
	 * ca_cert2 - File path to CA certificate file (PEM/DER) (Phase 2)
	 *
	 * This file can have one or more trusted CA certificates. If ca_cert2
	 * and ca_path2 are not included, server certificate will not be
	 * verified. This is insecure and a trusted CA certificate should
	 * always be configured. Full path to the file should be used since
	 * working directory may change when wpa_supplicant is run in the
	 * background.
	 *
	 * This field is like ca_cert, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication.
	 *
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://blob_name.
	 */
	u8 *ca_cert2;

	/**
	 * ca_path2 - Directory path for CA certificate files (PEM) (Phase 2)
	 *
	 * This path may contain multiple CA certificates in OpenSSL format.
	 * Common use for this is to point to system trusted CA list which is
	 * often installed into directory like /etc/ssl/certs. If configured,
	 * these certificates are added to the list of trusted CAs. ca_cert
	 * may also be included in that case, but it is not required.
	 *
	 * This field is like ca_path, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication.
	 */
	u8 *ca_path2;

	/**
	 * client_cert2 - File path to client certificate file
	 *
	 * This field is like client_cert, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication. Full path to the
	 * file should be used since working directory may change when
	 * wpa_supplicant is run in the background.
	 *
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://blob_name.
	 */
	u8 *client_cert2;

	/**
	 * private_key2 - File path to client private key file
	 *
	 * This field is like private_key, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication. Full path to the
	 * file should be used since working directory may change when
	 * wpa_supplicant is run in the background.
	 *
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://blob_name.
	 */
	u8 *private_key2;

	/**
	 * private_key2_passwd -  Password for private key file
	 *
	 * This field is like private_key_passwd, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication.
	 */
	u8 *private_key2_passwd;

	/**
	 * dh_file2 - File path to DH/DSA parameters file (in PEM format)
	 *
	 * This field is like dh_file, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication. Full path to the
	 * file should be used since working directory may change when
	 * wpa_supplicant is run in the background.
	 *
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://blob_name.
	 */
	u8 *dh_file2;

	/**
	 * subject_match2 - Constraint for server certificate subject
	 *
	 * This field is like subject_match, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication.
	 */
	u8 *subject_match2;

	/**
	 * altsubject_match2 - Constraint for server certificate alt. subject
	 *
	 * This field is like altsubject_match, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication.
	 */
	u8 *altsubject_match2;

	/**
	 * eap_methods - Allowed EAP methods
	 *
	 * (vendor=EAP_VENDOR_IETF,method=EAP_TYPE_NONE) terminated list of
	 * allowed EAP methods or %NULL if all methods are accepted.
	 */
	struct eap_method_type *eap_methods;

	/**
	 * phase1 - Phase 1 (outer authentication) parameters
	 *
	 * String with field-value pairs, e.g., "peapver=0" or
	 * "peapver=1 peaplabel=1".
	 *
	 * 'peapver' can be used to force which PEAP version (0 or 1) is used.
	 *
	 * 'peaplabel=1' can be used to force new label, "client PEAP
	 * encryption",	to be used during key derivation when PEAPv1 or newer.
	 *
	 * Most existing PEAPv1 implementation seem to be using the old label,
	 * "client EAP encryption", and wpa_supplicant is now using that as the
	 * default value.
	 *
	 * Some servers, e.g., Radiator, may require peaplabel=1 configuration
	 * to interoperate with PEAPv1; see eap_testing.txt for more details.
	 *
	 * 'peap_outer_success=0' can be used to terminate PEAP authentication
	 * on tunneled EAP-Success. This is required with some RADIUS servers
	 * that implement draft-josefsson-pppext-eap-tls-eap-05.txt (e.g.,
	 * Lucent NavisRadius v4.4.0 with PEAP in "IETF Draft 5" mode).
	 *
	 * include_tls_length=1 can be used to force wpa_supplicant to include
	 * TLS Message Length field in all TLS messages even if they are not
	 * fragmented.
	 *
	 * sim_min_num_chal=3 can be used to configure EAP-SIM to require three
	 * challenges (by default, it accepts 2 or 3).
	 *
	 * result_ind=1 can be used to enable EAP-SIM and EAP-AKA to use
	 * protected result indication.
	 *
	 * fast_provisioning option can be used to enable in-line provisioning
	 * of EAP-FAST credentials (PAC):
	 * 0 = disabled,
	 * 1 = allow unauthenticated provisioning,
	 * 2 = allow authenticated provisioning,
	 * 3 = allow both unauthenticated and authenticated provisioning
	 *
	 * fast_max_pac_list_len=num option can be used to set the maximum
	 * number of PAC entries to store in a PAC list (default: 10).
	 *
	 * fast_pac_format=binary option can be used to select binary format
	 * for storing PAC entries in order to save some space (the default
	 * text format uses about 2.5 times the size of minimal binary format).
	 *
	 * crypto_binding option can be used to control PEAPv0 cryptobinding
	 * behavior:
	 * 0 = do not use cryptobinding (default)
	 * 1 = use cryptobinding if server supports it
	 * 2 = require cryptobinding
	 *
	 * EAP-WSC (WPS) uses following options: pin=Device_Password and
	 * uuid=Device_UUID
	 */
	char *phase1;

	/**
	 * phase2 - Phase2 (inner authentication with TLS tunnel) parameters
	 *
	 * String with field-value pairs, e.g., "auth=MSCHAPV2" for EAP-PEAP or
	 * "autheap=MSCHAPV2 autheap=MD5" for EAP-TTLS.
	 */
	char *phase2;

	/**
	 * pcsc - Parameters for PC/SC smartcard interface for USIM and GSM SIM
	 *
	 * This field is used to configure PC/SC smartcard interface.
	 * Currently, the only configuration is whether this field is %NULL (do
	 * not use PC/SC) or non-NULL (e.g., "") to enable PC/SC.
	 *
	 * This field is used for EAP-SIM and EAP-AKA.
	 */
	char *pcsc;

	/**
	 * pin - PIN for USIM, GSM SIM, and smartcards
	 *
	 * This field is used to configure PIN for SIM and smartcards for
	 * EAP-SIM and EAP-AKA. In addition, this is used with EAP-TLS if a
	 * smartcard is used for private key operations.
	 *
	 * If left out, this will be asked through control interface.
	 */
	char *pin;

	/**
	 * engine - Enable OpenSSL engine (e.g., for smartcard access)
	 *
	 * This is used if private key operations for EAP-TLS are performed
	 * using a smartcard.
	 */
	int engine;

	/**
	 * engine_id - Engine ID for OpenSSL engine
	 *
	 * "opensc" to select OpenSC engine or "pkcs11" to select PKCS#11
	 * engine.
	 *
	 * This is used if private key operations for EAP-TLS are performed
	 * using a smartcard.
	 */
	char *engine_id;

	/**
	 * engine2 - Enable OpenSSL engine (e.g., for smartcard) (Phase 2)
	 *
	 * This is used if private key operations for EAP-TLS are performed
	 * using a smartcard.
	 *
	 * This field is like engine, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication.
	 */
	int engine2;


	/**
	 * pin2 - PIN for USIM, GSM SIM, and smartcards (Phase 2)
	 *
	 * This field is used to configure PIN for SIM and smartcards for
	 * EAP-SIM and EAP-AKA. In addition, this is used with EAP-TLS if a
	 * smartcard is used for private key operations.
	 *
	 * This field is like pin2, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication.
	 *
	 * If left out, this will be asked through control interface.
	 */
	char *pin2;

	/**
	 * engine2_id - Engine ID for OpenSSL engine (Phase 2)
	 *
	 * "opensc" to select OpenSC engine or "pkcs11" to select PKCS#11
	 * engine.
	 *
	 * This is used if private key operations for EAP-TLS are performed
	 * using a smartcard.
	 *
	 * This field is like engine_id, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication.
	 */
	char *engine2_id;


	/**
	 * key_id - Key ID for OpenSSL engine
	 *
	 * This is used if private key operations for EAP-TLS are performed
	 * using a smartcard.
	 */
	char *key_id;

	/**
	 * cert_id - Cert ID for OpenSSL engine
	 *
	 * This is used if the certificate operations for EAP-TLS are performed
	 * using a smartcard.
	 */
	char *cert_id;

	/**
	 * ca_cert_id - CA Cert ID for OpenSSL engine
	 *
	 * This is used if the CA certificate for EAP-TLS is on a smartcard.
	 */
	char *ca_cert_id;

	/**
	 * key2_id - Key ID for OpenSSL engine (phase2)
	 *
	 * This is used if private key operations for EAP-TLS are performed
	 * using a smartcard.
	 */
	char *key2_id;

	/**
	 * cert2_id - Cert ID for OpenSSL engine (phase2)
	 *
	 * This is used if the certificate operations for EAP-TLS are performed
	 * using a smartcard.
	 */
	char *cert2_id;

	/**
	 * ca_cert2_id - CA Cert ID for OpenSSL engine (phase2)
	 *
	 * This is used if the CA certificate for EAP-TLS is on a smartcard.
	 */
	char *ca_cert2_id;

	/**
	 * otp - One-time-password
	 *
	 * This field should not be set in configuration step. It is only used
	 * internally when OTP is entered through the control interface.
	 */
	u8 *otp;

	/**
	 * otp_len - Length of the otp field
	 */
	size_t otp_len;

	/**
	 * pending_req_identity - Whether there is a pending identity request
	 *
	 * This field should not be set in configuration step. It is only used
	 * internally when control interface is used to request needed
	 * information.
	 */
	int pending_req_identity;

	/**
	 * pending_req_password - Whether there is a pending password request
	 *
	 * This field should not be set in configuration step. It is only used
	 * internally when control interface is used to request needed
	 * information.
	 */
	int pending_req_password;

	/**
	 * pending_req_pin - Whether there is a pending PIN request
	 *
	 * This field should not be set in configuration step. It is only used
	 * internally when control interface is used to request needed
	 * information.
	 */
	int pending_req_pin;

	/**
	 * pending_req_new_password - Pending password update request
	 *
	 * This field should not be set in configuration step. It is only used
	 * internally when control interface is used to request needed
	 * information.
	 */
	int pending_req_new_password;

	/**
	 * pending_req_passphrase - Pending passphrase request
	 *
	 * This field should not be set in configuration step. It is only used
	 * internally when control interface is used to request needed
	 * information.
	 */
	int pending_req_passphrase;

	/**
	 * pending_req_otp - Whether there is a pending OTP request
	 *
	 * This field should not be set in configuration step. It is only used
	 * internally when control interface is used to request needed
	 * information.
	 */
	char *pending_req_otp;

	/**
	 * pending_req_otp_len - Length of the pending OTP request
	 */
	size_t pending_req_otp_len;

	/**
	 * pac_file - File path or blob name for the PAC entries (EAP-FAST)
	 *
	 * wpa_supplicant will need to be able to create this file and write
	 * updates to it when PAC is being provisioned or refreshed. Full path
	 * to the file should be used since working directory may change when
	 * wpa_supplicant is run in the background.
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://blob_name.
	 */
	char *pac_file;

	/**
	 * mschapv2_retry - MSCHAPv2 retry in progress
	 *
	 * This field is used internally by EAP-MSCHAPv2 and should not be set
	 * as part of configuration.
	 */
	int mschapv2_retry;

	/**
	 * new_password - New password for password update
	 *
	 * This field is used during MSCHAPv2 password update. This is normally
	 * requested from the user through the control interface and not set
	 * from configuration.
	 */
	u8 *new_password;

	/**
	 * new_password_len - Length of new_password field
	 */
	size_t new_password_len;

	/**
	 * fragment_size - Maximum EAP fragment size in bytes (default 1398)
	 *
	 * This value limits the fragment size for EAP methods that support
	 * fragmentation (e.g., EAP-TLS and EAP-PEAP). This value should be set
	 * small enough to make the EAP messages fit in MTU of the network
	 * interface used for EAPOL. The default value is suitable for most
	 * cases.
	 */
	int fragment_size;

#define EAP_CONFIG_FLAGS_PASSWORD_NTHASH BIT(0)
	/**
	 * flags - Network configuration flags (bitfield)
	 *
	 * This variable is used for internal flags to describe further details
	 * for the network parameters.
	 * bit 0 = password is represented as a 16-byte NtPasswordHash value
	 *         instead of plaintext password
	 */
	u32 flags;
};


/**
 * struct wpa_config_blob - Named configuration blob
 *
 * This data structure is used to provide storage for binary objects to store
 * abstract information like certificates and private keys inlined with the
 * configuration data.
 */
struct wpa_config_blob {
	/**
	 * name - Blob name
	 */
	char *name;

	/**
	 * data - Pointer to binary data
	 */
	u8 *data;

	/**
	 * len - Length of binary data
	 */
	size_t len;

	/**
	 * next - Pointer to next blob in the configuration
	 */
	struct wpa_config_blob *next;
};

#endif /* EAP_CONFIG_H */
