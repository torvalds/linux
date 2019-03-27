/*
 * TLSv1 client - OCSP
 * Copyright (c) 2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/tls.h"
#include "crypto/sha1.h"
#include "asn1.h"
#include "x509v3.h"
#include "tlsv1_common.h"
#include "tlsv1_record.h"
#include "tlsv1_client.h"
#include "tlsv1_client_i.h"


/* RFC 6960, 4.2.1: OCSPResponseStatus ::= ENUMERATED */
enum ocsp_response_status {
	OCSP_RESP_STATUS_SUCCESSFUL = 0,
	OCSP_RESP_STATUS_MALFORMED_REQ = 1,
	OCSP_RESP_STATUS_INT_ERROR = 2,
	OCSP_RESP_STATUS_TRY_LATER = 3,
	/* 4 not used */
	OCSP_RESP_STATUS_SIG_REQUIRED = 5,
	OCSP_RESP_STATUS_UNAUTHORIZED = 6,
};


static int is_oid_basic_ocsp_resp(struct asn1_oid *oid)
{
	return oid->len == 10 &&
		oid->oid[0] == 1 /* iso */ &&
		oid->oid[1] == 3 /* identified-organization */ &&
		oid->oid[2] == 6 /* dod */ &&
		oid->oid[3] == 1 /* internet */ &&
		oid->oid[4] == 5 /* security */ &&
		oid->oid[5] == 5 /* mechanisms */ &&
		oid->oid[6] == 7 /* id-pkix */ &&
		oid->oid[7] == 48 /* id-ad */ &&
		oid->oid[8] == 1 /* id-pkix-ocsp */ &&
		oid->oid[9] == 1 /* id-pkix-ocsp-basic */;
}


static int ocsp_responder_id_match(struct x509_certificate *signer,
				   struct x509_name *name, const u8 *key_hash)
{
	if (key_hash) {
		u8 hash[SHA1_MAC_LEN];
		const u8 *addr[1] = { signer->public_key };
		size_t len[1] = { signer->public_key_len };

		if (sha1_vector(1, addr, len, hash) < 0)
			return 0;
		return os_memcmp(hash, key_hash, SHA1_MAC_LEN) == 0;
	}

	return x509_name_compare(&signer->subject, name) == 0;
}


static unsigned int ocsp_hash_data(struct asn1_oid *alg, const u8 *data,
				   size_t data_len, u8 *hash)
{
	const u8 *addr[1] = { data };
	size_t len[1] = { data_len };
	char buf[100];

	if (x509_sha1_oid(alg)) {
		if (sha1_vector(1, addr, len, hash) < 0)
			return 0;
		wpa_hexdump(MSG_MSGDUMP, "OCSP: Hash (SHA1)", hash, 20);
		return 20;
	}

	if (x509_sha256_oid(alg)) {
		if (sha256_vector(1, addr, len, hash) < 0)
			return 0;
		wpa_hexdump(MSG_MSGDUMP, "OCSP: Hash (SHA256)", hash, 32);
		return 32;
	}

	if (x509_sha384_oid(alg)) {
		if (sha384_vector(1, addr, len, hash) < 0)
			return 0;
		wpa_hexdump(MSG_MSGDUMP, "OCSP: Hash (SHA384)", hash, 48);
		return 48;
	}

	if (x509_sha512_oid(alg)) {
		if (sha512_vector(1, addr, len, hash) < 0)
			return 0;
		wpa_hexdump(MSG_MSGDUMP, "OCSP: Hash (SHA512)", hash, 64);
		return 64;
	}


	asn1_oid_to_str(alg, buf, sizeof(buf));
	wpa_printf(MSG_DEBUG, "OCSP: Could not calculate hash with alg %s",
		   buf);
	return 0;
}


static int tls_process_ocsp_single_response(struct tlsv1_client *conn,
					    struct x509_certificate *cert,
					    struct x509_certificate *issuer,
					    const u8 *resp, size_t len,
					    enum tls_ocsp_result *res)
{
	struct asn1_hdr hdr;
	const u8 *pos, *end;
	struct x509_algorithm_identifier alg;
	const u8 *name_hash, *key_hash;
	size_t name_hash_len, key_hash_len;
	const u8 *serial_number;
	size_t serial_number_len;
	u8 hash[64];
	unsigned int hash_len;
	unsigned int cert_status;
	os_time_t update;
	struct os_time now;

	wpa_hexdump(MSG_MSGDUMP, "OCSP: SingleResponse", resp, len);

	/*
	 * SingleResponse ::= SEQUENCE {
	 *    certID                       CertID,
	 *    certStatus                   CertStatus,
	 *    thisUpdate                   GeneralizedTime,
	 *    nextUpdate         [0]       EXPLICIT GeneralizedTime OPTIONAL,
	 *    singleExtensions   [1]       EXPLICIT Extensions OPTIONAL }
	 */

	/* CertID ::= SEQUENCE */
	if (asn1_get_next(resp, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Expected SEQUENCE (CertID) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	pos = hdr.payload;
	end = hdr.payload + hdr.length;

	/*
	 * CertID ::= SEQUENCE {
	 *    hashAlgorithm           AlgorithmIdentifier,
	 *    issuerNameHash          OCTET STRING,
	 *    issuerKeyHash           OCTET STRING,
	 *    serialNumber            CertificateSerialNumber }
	 */

	/* hashAlgorithm  AlgorithmIdentifier */
	if (x509_parse_algorithm_identifier(pos, end - pos, &alg, &pos))
		return -1;

	/* issuerNameHash  OCTET STRING */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_OCTETSTRING) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Expected OCTET STRING (issuerNameHash) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	name_hash = hdr.payload;
	name_hash_len = hdr.length;
	wpa_hexdump(MSG_DEBUG, "OCSP: issuerNameHash",
		    name_hash, name_hash_len);
	pos = hdr.payload + hdr.length;

	wpa_hexdump(MSG_DEBUG, "OCSP: Issuer subject DN",
		    issuer->subject_dn, issuer->subject_dn_len);
	hash_len = ocsp_hash_data(&alg.oid, issuer->subject_dn,
				  issuer->subject_dn_len, hash);
	if (hash_len == 0 || name_hash_len != hash_len ||
	    os_memcmp(name_hash, hash, hash_len) != 0) {
		wpa_printf(MSG_DEBUG, "OCSP: issuerNameHash mismatch");
		wpa_hexdump(MSG_DEBUG, "OCSP: Calculated issuerNameHash",
			    hash, hash_len);
		return -1;
	}

	/* issuerKeyHash  OCTET STRING */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_OCTETSTRING) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Expected OCTET STRING (issuerKeyHash) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	key_hash = hdr.payload;
	key_hash_len = hdr.length;
	wpa_hexdump(MSG_DEBUG, "OCSP: issuerKeyHash", key_hash, key_hash_len);
	pos = hdr.payload + hdr.length;

	hash_len = ocsp_hash_data(&alg.oid, issuer->public_key,
				  issuer->public_key_len, hash);
	if (hash_len == 0 || key_hash_len != hash_len ||
	    os_memcmp(key_hash, hash, hash_len) != 0) {
		wpa_printf(MSG_DEBUG, "OCSP: issuerKeyHash mismatch");
		wpa_hexdump(MSG_DEBUG, "OCSP: Calculated issuerKeyHash",
			    hash, hash_len);
		return -1;
	}

	/* serialNumber CertificateSerialNumber ::= INTEGER */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_INTEGER ||
	    hdr.length < 1 || hdr.length > X509_MAX_SERIAL_NUM_LEN) {
		wpa_printf(MSG_DEBUG, "OCSP: No INTEGER tag found for serialNumber; class=%d tag=0x%x length=%u",
			   hdr.class, hdr.tag, hdr.length);
		return -1;
	}
	serial_number = hdr.payload;
	serial_number_len = hdr.length;
	while (serial_number_len > 0 && serial_number[0] == 0) {
		serial_number++;
		serial_number_len--;
	}
	wpa_hexdump(MSG_MSGDUMP, "OCSP: serialNumber", serial_number,
		    serial_number_len);

	if (serial_number_len != cert->serial_number_len ||
	    os_memcmp(serial_number, cert->serial_number,
		      serial_number_len) != 0) {
		wpa_printf(MSG_DEBUG, "OCSP: serialNumber mismatch");
		return -1;
	}

	pos = end;
	end = resp + len;

	/* certStatus CertStatus ::= CHOICE */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_CONTEXT_SPECIFIC) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Expected CHOICE (CertStatus) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	cert_status = hdr.tag;
	wpa_printf(MSG_DEBUG, "OCSP: certStatus=%u", cert_status);
	wpa_hexdump(MSG_DEBUG, "OCSP: CertStatus additional data",
		    hdr.payload, hdr.length);
	pos = hdr.payload + hdr.length;

	os_get_time(&now);
	/* thisUpdate  GeneralizedTime */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_GENERALIZEDTIME ||
	    x509_parse_time(hdr.payload, hdr.length, hdr.tag, &update) < 0) {
		wpa_printf(MSG_DEBUG, "OCSP: Failed to parse thisUpdate");
		return -1;
	}
	wpa_printf(MSG_DEBUG, "OCSP: thisUpdate %lu", (unsigned long) update);
	pos = hdr.payload + hdr.length;
	if ((unsigned long) now.sec < (unsigned long) update) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: thisUpdate time in the future (response not yet valid)");
		return -1;
	}

	/* nextUpdate  [0]  EXPLICIT GeneralizedTime OPTIONAL */
	if (pos < end) {
		if (asn1_get_next(pos, end - pos, &hdr) < 0)
			return -1;
		if (hdr.class == ASN1_CLASS_CONTEXT_SPECIFIC && hdr.tag == 0) {
			const u8 *next = hdr.payload + hdr.length;

			if (asn1_get_next(hdr.payload, hdr.length, &hdr) < 0 ||
			    hdr.class != ASN1_CLASS_UNIVERSAL ||
			    hdr.tag != ASN1_TAG_GENERALIZEDTIME ||
			    x509_parse_time(hdr.payload, hdr.length, hdr.tag,
					    &update) < 0) {
				wpa_printf(MSG_DEBUG,
					   "OCSP: Failed to parse nextUpdate");
				return -1;
			}
			wpa_printf(MSG_DEBUG, "OCSP: nextUpdate %lu",
				   (unsigned long) update);
			pos = next;
			if ((unsigned long) now.sec > (unsigned long) update) {
				wpa_printf(MSG_DEBUG, "OCSP: nextUpdate time in the past (response has expired)");
				return -1;
			}
		}
	}

	/* singleExtensions  [1]  EXPLICIT Extensions OPTIONAL */
	if (pos < end) {
		wpa_hexdump(MSG_MSGDUMP, "OCSP: singleExtensions",
			    pos, end - pos);
		/* Ignore for now */
	}

	if (cert_status == 0 /* good */)
		*res = TLS_OCSP_GOOD;
	else if (cert_status == 1 /* revoked */)
		*res = TLS_OCSP_REVOKED;
	else
		return -1;
	return 0;
}


static enum tls_ocsp_result
tls_process_ocsp_responses(struct tlsv1_client *conn,
			   struct x509_certificate *cert,
			   struct x509_certificate *issuer, const u8 *resp,
			   size_t len)
{
	struct asn1_hdr hdr;
	const u8 *pos, *end;
	enum tls_ocsp_result res;

	pos = resp;
	end = resp + len;
	while (pos < end) {
		/* SingleResponse ::= SEQUENCE */
		if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
		    hdr.class != ASN1_CLASS_UNIVERSAL ||
		    hdr.tag != ASN1_TAG_SEQUENCE) {
			wpa_printf(MSG_DEBUG,
				   "OCSP: Expected SEQUENCE (SingleResponse) - found class %d tag 0x%x",
				   hdr.class, hdr.tag);
			return TLS_OCSP_INVALID;
		}
		if (tls_process_ocsp_single_response(conn, cert, issuer,
						     hdr.payload, hdr.length,
						     &res) == 0)
			return res;
		pos = hdr.payload + hdr.length;
	}

	wpa_printf(MSG_DEBUG,
		   "OCSP: Did not find a response matching the server certificate");
	return TLS_OCSP_NO_RESPONSE;
}


static enum tls_ocsp_result
tls_process_basic_ocsp_response(struct tlsv1_client *conn,
				struct x509_certificate *srv_cert,
				const u8 *resp, size_t len)
{
	struct asn1_hdr hdr;
	const u8 *pos, *end;
	const u8 *resp_data, *sign_value, *key_hash = NULL, *responses;
	const u8 *resp_data_signed;
	size_t resp_data_len, sign_value_len, responses_len;
	size_t resp_data_signed_len;
	struct x509_algorithm_identifier alg;
	struct x509_certificate *certs = NULL, *last_cert = NULL;
	struct x509_certificate *issuer, *signer;
	struct x509_name name; /* used if key_hash == NULL */
	char buf[100];
	os_time_t produced_at;
	enum tls_ocsp_result res;

	wpa_hexdump(MSG_MSGDUMP, "OCSP: BasicOCSPResponse", resp, len);

	os_memset(&name, 0, sizeof(name));

	/*
	 * RFC 6960, 4.2.1:
	 * BasicOCSPResponse       ::= SEQUENCE {
	 *    tbsResponseData      ResponseData,
	 *    signatureAlgorithm   AlgorithmIdentifier,
	 *    signature            BIT STRING,
	 *    certs            [0] EXPLICIT SEQUENCE OF Certificate OPTIONAL }
	 */

	if (asn1_get_next(resp, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Expected SEQUENCE (BasicOCSPResponse) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return TLS_OCSP_INVALID;
	}
	pos = hdr.payload;
	end = hdr.payload + hdr.length;

	/* ResponseData ::= SEQUENCE */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Expected SEQUENCE (ResponseData) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return TLS_OCSP_INVALID;
	}
	resp_data = hdr.payload;
	resp_data_len = hdr.length;
	resp_data_signed = pos;
	pos = hdr.payload + hdr.length;
	resp_data_signed_len = pos - resp_data_signed;

	/* signatureAlgorithm  AlgorithmIdentifier */
	if (x509_parse_algorithm_identifier(pos, end - pos, &alg, &pos))
		return TLS_OCSP_INVALID;

	/* signature  BIT STRING */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_BITSTRING) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Expected BITSTRING (signature) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return TLS_OCSP_INVALID;
	}
	if (hdr.length < 1)
		return TLS_OCSP_INVALID;
	pos = hdr.payload;
	if (*pos) {
		wpa_printf(MSG_DEBUG, "OCSP: BITSTRING - %d unused bits", *pos);
		/* PKCS #1 v1.5 10.2.1:
		 * It is an error if the length in bits of the signature S is
		 * not a multiple of eight.
		 */
		return TLS_OCSP_INVALID;
	}
	sign_value = pos + 1;
	sign_value_len = hdr.length - 1;
	pos += hdr.length;
	wpa_hexdump(MSG_MSGDUMP, "OCSP: signature", sign_value, sign_value_len);

	/* certs  [0] EXPLICIT SEQUENCE OF Certificate OPTIONAL */
	if (pos < end) {
		if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
		    hdr.class != ASN1_CLASS_CONTEXT_SPECIFIC ||
		    hdr.tag != 0) {
			wpa_printf(MSG_DEBUG,
				   "OCSP: Expected [0] EXPLICIT (certs) - found class %d tag 0x%x",
				   hdr.class, hdr.tag);
			return TLS_OCSP_INVALID;
		}
		wpa_hexdump(MSG_MSGDUMP, "OCSP: certs",
			    hdr.payload, hdr.length);
		pos = hdr.payload;
		end = hdr.payload + hdr.length;
		while (pos < end) {
			struct x509_certificate *cert;

			if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
			    hdr.class != ASN1_CLASS_UNIVERSAL ||
			    hdr.tag != ASN1_TAG_SEQUENCE) {
				wpa_printf(MSG_DEBUG,
					   "OCSP: Expected SEQUENCE (Certificate) - found class %d tag 0x%x",
					   hdr.class, hdr.tag);
				goto fail;
			}

			cert = x509_certificate_parse(hdr.payload, hdr.length);
			if (!cert)
				goto fail;
			if (last_cert) {
				last_cert->next = cert;
				last_cert = cert;
			} else {
				last_cert = certs = cert;
			}
			pos = hdr.payload + hdr.length;
		}
	}

	/*
	 * ResponseData ::= SEQUENCE {
	 *    version              [0] EXPLICIT Version DEFAULT v1,
	 *    responderID              ResponderID,
	 *    producedAt               GeneralizedTime,
	 *    responses                SEQUENCE OF SingleResponse,
	 *    responseExtensions   [1] EXPLICIT Extensions OPTIONAL }
	 */
	pos = resp_data;
	end = resp_data + resp_data_len;
	wpa_hexdump(MSG_MSGDUMP, "OCSP: ResponseData", pos, end - pos);

	/*
	 * version [0] EXPLICIT Version DEFAULT v1
	 * Version ::= INTEGER { v1(0) }
	 */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 &&
	    hdr.class == ASN1_CLASS_CONTEXT_SPECIFIC &&
	    hdr.tag == 0) {
		if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
		    hdr.class != ASN1_CLASS_UNIVERSAL ||
		    hdr.tag != ASN1_TAG_INTEGER ||
		    hdr.length != 1) {
			wpa_printf(MSG_DEBUG,
				   "OCSP: No INTEGER (len=1) tag found for version field - found class %d tag 0x%x length %d",
				   hdr.class, hdr.tag, hdr.length);
			goto fail;
		}
		wpa_printf(MSG_DEBUG, "OCSP: ResponseData version %u",
			   hdr.payload[0]);
		if (hdr.payload[0] != 0) {
			wpa_printf(MSG_DEBUG,
				   "OCSP: Unsupported ResponseData version %u",
				   hdr.payload[0]);
			goto no_resp;
		}
		pos = hdr.payload + hdr.length;
	} else {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Default ResponseData version (v1)");
	}

	/*
	 * ResponderID ::= CHOICE {
	 *    byName              [1] Name,
	 *    byKey               [2] KeyHash }
	 */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_CONTEXT_SPECIFIC) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Expected CHOICE (ResponderID) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		goto fail;
	}

	if (hdr.tag == 1) {
		/* Name */
		if (x509_parse_name(hdr.payload, hdr.length, &name, &pos) < 0)
			goto fail;
		x509_name_string(&name, buf, sizeof(buf));
		wpa_printf(MSG_DEBUG, "OCSP: ResponderID byName Name: %s", buf);
	} else if (hdr.tag == 2) {
		/* KeyHash ::= OCTET STRING */
		if (asn1_get_next(hdr.payload, hdr.length, &hdr) < 0 ||
		    hdr.class != ASN1_CLASS_UNIVERSAL ||
		    hdr.tag != ASN1_TAG_OCTETSTRING) {
			wpa_printf(MSG_DEBUG,
				   "OCSP: Expected OCTET STRING (KeyHash) - found class %d tag 0x%x",
				   hdr.class, hdr.tag);
			goto fail;
		}
		key_hash = hdr.payload;
		wpa_hexdump(MSG_DEBUG, "OCSP: ResponderID byKey KeyHash",
			    key_hash, hdr.length);
		if (hdr.length != SHA1_MAC_LEN) {
			wpa_printf(MSG_DEBUG,
				   "OCSP: Unexpected byKey KeyHash length %u - expected %u for SHA-1",
				   hdr.length, SHA1_MAC_LEN);
			goto fail;
		}
		pos = hdr.payload + hdr.length;
	} else {
		wpa_printf(MSG_DEBUG, "OCSP: Unexpected ResponderID CHOICE %u",
			   hdr.tag);
		goto fail;
	}

	/* producedAt  GeneralizedTime */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_GENERALIZEDTIME ||
	    x509_parse_time(hdr.payload, hdr.length, hdr.tag,
			    &produced_at) < 0) {
		wpa_printf(MSG_DEBUG, "OCSP: Failed to parse producedAt");
		goto fail;
	}
	wpa_printf(MSG_DEBUG, "OCSP: producedAt %lu",
		   (unsigned long) produced_at);
	pos = hdr.payload + hdr.length;

	/* responses  SEQUENCE OF SingleResponse */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Expected SEQUENCE (responses) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		goto fail;
	}
	responses = hdr.payload;
	responses_len = hdr.length;
	wpa_hexdump(MSG_MSGDUMP, "OCSP: responses", responses, responses_len);
	pos = hdr.payload + hdr.length;

	if (pos < end) {
		/* responseExtensions  [1] EXPLICIT Extensions OPTIONAL */
		wpa_hexdump(MSG_MSGDUMP, "OCSP: responseExtensions",
			    pos, end - pos);
		/* Ignore for now. */
	}

	if (!srv_cert) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Server certificate not known - cannot check OCSP response");
		goto no_resp;
	}

	if (srv_cert->next) {
		/* Issuer has already been verified in the chain */
		issuer = srv_cert->next;
	} else {
		/* Find issuer from the set of trusted certificates */
		for (issuer = conn->cred ? conn->cred->trusted_certs : NULL;
		     issuer; issuer = issuer->next) {
			if (x509_name_compare(&srv_cert->issuer,
					      &issuer->subject) == 0)
				break;
		}
	}
	if (!issuer) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Server certificate issuer not known - cannot check OCSP response");
		goto no_resp;
	}

	if (ocsp_responder_id_match(issuer, &name, key_hash)) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Server certificate issuer certificate matches ResponderID");
		signer = issuer;
	} else {
		for (signer = certs; signer; signer = signer->next) {
			if (!ocsp_responder_id_match(signer, &name, key_hash) ||
			    x509_name_compare(&srv_cert->issuer,
					      &issuer->subject) != 0 ||
			    !(signer->ext_key_usage &
			      X509_EXT_KEY_USAGE_OCSP) ||
			    x509_certificate_check_signature(issuer, signer) <
			    0)
				continue;
			wpa_printf(MSG_DEBUG,
				   "OCSP: An extra certificate from the response matches ResponderID and is trusted as an OCSP signer");
			break;
		}
		if (!signer) {
			wpa_printf(MSG_DEBUG,
				   "OCSP: Could not find OCSP signer certificate");
			goto no_resp;
		}
	}

	x509_free_name(&name);
	os_memset(&name, 0, sizeof(name));
	x509_certificate_chain_free(certs);
	certs = NULL;

	if (x509_check_signature(signer, &alg, sign_value, sign_value_len,
				 resp_data_signed, resp_data_signed_len) < 0) {
		    wpa_printf(MSG_DEBUG, "OCSP: Invalid signature");
		    return TLS_OCSP_INVALID;
	}

	res = tls_process_ocsp_responses(conn, srv_cert, issuer,
					 responses, responses_len);
	if (res == TLS_OCSP_REVOKED)
		srv_cert->ocsp_revoked = 1;
	else if (res == TLS_OCSP_GOOD)
		srv_cert->ocsp_good = 1;
	return res;

no_resp:
	x509_free_name(&name);
	x509_certificate_chain_free(certs);
	return TLS_OCSP_NO_RESPONSE;

fail:
	x509_free_name(&name);
	x509_certificate_chain_free(certs);
	return TLS_OCSP_INVALID;
}


enum tls_ocsp_result tls_process_ocsp_response(struct tlsv1_client *conn,
					       const u8 *resp, size_t len)
{
	struct asn1_hdr hdr;
	const u8 *pos, *end;
	u8 resp_status;
	struct asn1_oid oid;
	char obuf[80];
	struct x509_certificate *cert;
	enum tls_ocsp_result res = TLS_OCSP_NO_RESPONSE;
	enum tls_ocsp_result res_first = res;

	wpa_hexdump(MSG_MSGDUMP, "TLSv1: OCSPResponse", resp, len);

	/*
	 * RFC 6960, 4.2.1:
	 * OCSPResponse ::= SEQUENCE {
	 *    responseStatus  OCSPResponseStatus,
	 *    responseBytes   [0] EXPLICIT ResponseBytes OPTIONAL }
	 */

	if (asn1_get_next(resp, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Expected SEQUENCE (OCSPResponse) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return TLS_OCSP_INVALID;
	}
	pos = hdr.payload;
	end = hdr.payload + hdr.length;

	/* OCSPResponseStatus ::= ENUMERATED */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_ENUMERATED ||
	    hdr.length != 1) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Expected ENUMERATED (responseStatus) - found class %d tag 0x%x length %u",
			   hdr.class, hdr.tag, hdr.length);
		return TLS_OCSP_INVALID;
	}
	resp_status = hdr.payload[0];
	wpa_printf(MSG_DEBUG, "OCSP: responseStatus %u", resp_status);
	pos = hdr.payload + hdr.length;
	if (resp_status != OCSP_RESP_STATUS_SUCCESSFUL) {
		wpa_printf(MSG_DEBUG, "OCSP: No stapling result");
		return TLS_OCSP_NO_RESPONSE;
	}

	/* responseBytes   [0] EXPLICIT ResponseBytes OPTIONAL */
	if (pos == end)
		return TLS_OCSP_NO_RESPONSE;

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_CONTEXT_SPECIFIC ||
	    hdr.tag != 0) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Expected [0] EXPLICIT (responseBytes) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return TLS_OCSP_INVALID;
	}

	/*
	 * ResponseBytes ::= SEQUENCE {
	 *     responseType   OBJECT IDENTIFIER,
	 *     response       OCTET STRING }
	 */

	if (asn1_get_next(hdr.payload, hdr.length, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Expected SEQUENCE (ResponseBytes) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return TLS_OCSP_INVALID;
	}
	pos = hdr.payload;
	end = hdr.payload + hdr.length;

	/* responseType   OBJECT IDENTIFIER */
	if (asn1_get_oid(pos, end - pos, &oid, &pos)) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Failed to parse OID (responseType)");
		return TLS_OCSP_INVALID;
	}
	asn1_oid_to_str(&oid, obuf, sizeof(obuf));
	wpa_printf(MSG_DEBUG, "OCSP: responseType %s", obuf);
	if (!is_oid_basic_ocsp_resp(&oid)) {
		wpa_printf(MSG_DEBUG, "OCSP: Ignore unsupported response type");
		return TLS_OCSP_NO_RESPONSE;
	}

	/* response       OCTET STRING */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_OCTETSTRING) {
		wpa_printf(MSG_DEBUG,
			   "OCSP: Expected OCTET STRING (response) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return TLS_OCSP_INVALID;
	}

	cert = conn->server_cert;
	while (cert) {
		if (!cert->ocsp_good && !cert->ocsp_revoked) {
			char sbuf[128];

			x509_name_string(&cert->subject, sbuf, sizeof(sbuf));
			wpa_printf(MSG_DEBUG,
				   "OCSP: Trying to find certificate status for %s",
				   sbuf);

			res = tls_process_basic_ocsp_response(conn, cert,
							      hdr.payload,
							      hdr.length);
			if (cert == conn->server_cert)
				res_first = res;
		}
		if (res == TLS_OCSP_REVOKED || cert->issuer_trusted)
			break;
		cert = cert->next;
	}
	return res == TLS_OCSP_REVOKED ? res : res_first;
}
