/*
 * Hotspot 2.0 OSU client
 * Copyright (c) 2012-2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <time.h>
#include <sys/stat.h>
#ifdef ANDROID
#include "private/android_filesystem_config.h"
#endif /* ANDROID */

#include "common.h"
#include "utils/browser.h"
#include "utils/base64.h"
#include "utils/xml-utils.h"
#include "utils/http-utils.h"
#include "common/wpa_ctrl.h"
#include "common/wpa_helpers.h"
#include "eap_common/eap_defs.h"
#include "crypto/crypto.h"
#include "crypto/sha256.h"
#include "osu_client.h"

const char *spp_xsd_fname = "spp.xsd";


void write_result(struct hs20_osu_client *ctx, const char *fmt, ...)
{
	va_list ap;
	FILE *f;
	char buf[500];

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	write_summary(ctx, "%s", buf);

	if (!ctx->result_file)
		return;

	f = fopen(ctx->result_file, "w");
	if (f == NULL)
		return;

	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fprintf(f, "\n");
	fclose(f);
}


void write_summary(struct hs20_osu_client *ctx, const char *fmt, ...)
{
	va_list ap;
	FILE *f;

	if (!ctx->summary_file)
		return;

	f = fopen(ctx->summary_file, "a");
	if (f == NULL)
		return;

	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fprintf(f, "\n");
	fclose(f);
}


void debug_dump_node(struct hs20_osu_client *ctx, const char *title,
		     xml_node_t *node)
{
	char *str = xml_node_to_str(ctx->xml, node);
	wpa_printf(MSG_DEBUG, "[hs20] %s: '%s'", title, str);
	free(str);
}


static int valid_fqdn(const char *fqdn)
{
	const char *pos;

	/* TODO: could make this more complete.. */
	if (strchr(fqdn, '.') == 0 || strlen(fqdn) > 255)
		return 0;
	for (pos = fqdn; *pos; pos++) {
		if (*pos >= 'a' && *pos <= 'z')
			continue;
		if (*pos >= 'A' && *pos <= 'Z')
			continue;
		if (*pos >= '0' && *pos <= '9')
			continue;
		if (*pos == '-' || *pos == '.' || *pos == '_')
			continue;
		return 0;
	}
	return 1;
}


static int android_update_permission(const char *path, mode_t mode)
{
#ifdef ANDROID
	/* we need to change file/folder permission for Android */

	if (!path) {
		wpa_printf(MSG_ERROR, "file path null");
		return -1;
	}

	/* Allow processes running with Group ID as AID_WIFI,
	 * to read files from SP, SP/<fqdn>, Cert and osu-info directories */
	if (chown(path, -1, AID_WIFI)) {
		wpa_printf(MSG_INFO, "CTRL: Could not chown directory: %s",
			   strerror(errno));
		return -1;
	}

	if (chmod(path, mode) < 0) {
		wpa_printf(MSG_INFO, "CTRL: Could not chmod directory: %s",
			   strerror(errno));
		return -1;
	}
#endif  /* ANDROID */

	return 0;
}


int osu_get_certificate(struct hs20_osu_client *ctx, xml_node_t *getcert)
{
	xml_node_t *node;
	char *url, *user = NULL, *pw = NULL;
	char *proto;
	int ret = -1;

	proto = xml_node_get_attr_value(ctx->xml, getcert,
					"enrollmentProtocol");
	if (!proto)
		return -1;
	wpa_printf(MSG_INFO, "getCertificate - enrollmentProtocol=%s", proto);
	write_summary(ctx, "getCertificate - enrollmentProtocol=%s", proto);
	if (os_strcasecmp(proto, "EST") != 0) {
		wpa_printf(MSG_INFO, "Unsupported enrollmentProtocol");
		xml_node_get_attr_value_free(ctx->xml, proto);
		return -1;
	}
	xml_node_get_attr_value_free(ctx->xml, proto);

	node = get_node(ctx->xml, getcert, "enrollmentServerURI");
	if (node == NULL) {
		wpa_printf(MSG_INFO, "Could not find enrollmentServerURI node");
		xml_node_get_attr_value_free(ctx->xml, proto);
		return -1;
	}
	url = xml_node_get_text(ctx->xml, node);
	if (url == NULL) {
		wpa_printf(MSG_INFO, "Could not get URL text");
		return -1;
	}
	wpa_printf(MSG_INFO, "enrollmentServerURI: %s", url);
	write_summary(ctx, "enrollmentServerURI: %s", url);

	node = get_node(ctx->xml, getcert, "estUserID");
	if (node == NULL && !ctx->client_cert_present) {
		wpa_printf(MSG_INFO, "Could not find estUserID node");
		goto fail;
	}
	if (node) {
		user = xml_node_get_text(ctx->xml, node);
		if (user == NULL) {
			wpa_printf(MSG_INFO, "Could not get estUserID text");
			goto fail;
		}
		wpa_printf(MSG_INFO, "estUserID: %s", user);
		write_summary(ctx, "estUserID: %s", user);
	}

	node = get_node(ctx->xml, getcert, "estPassword");
	if (node == NULL && !ctx->client_cert_present) {
		wpa_printf(MSG_INFO, "Could not find estPassword node");
		goto fail;
	}
	if (node) {
		pw = xml_node_get_base64_text(ctx->xml, node, NULL);
		if (pw == NULL) {
			wpa_printf(MSG_INFO, "Could not get estPassword text");
			goto fail;
		}
		wpa_printf(MSG_INFO, "estPassword: %s", pw);
	}

	mkdir("Cert", S_IRWXU);
	android_update_permission("Cert", S_IRWXU | S_IRWXG);

	if (est_load_cacerts(ctx, url) < 0 ||
	    est_build_csr(ctx, url) < 0 ||
	    est_simple_enroll(ctx, url, user, pw) < 0)
		goto fail;

	ret = 0;
fail:
	xml_node_get_text_free(ctx->xml, url);
	xml_node_get_text_free(ctx->xml, user);
	xml_node_get_text_free(ctx->xml, pw);

	return ret;
}


static int process_est_cert(struct hs20_osu_client *ctx, xml_node_t *cert,
			    const char *fqdn)
{
	u8 digest1[SHA256_MAC_LEN], digest2[SHA256_MAC_LEN];
	char *der, *pem;
	size_t der_len, pem_len;
	char *fingerprint;
	char buf[200];

	wpa_printf(MSG_INFO, "PPS for certificate credential - fqdn=%s", fqdn);

	fingerprint = xml_node_get_text(ctx->xml, cert);
	if (fingerprint == NULL)
		return -1;
	if (hexstr2bin(fingerprint, digest1, SHA256_MAC_LEN) < 0) {
		wpa_printf(MSG_INFO, "Invalid SHA256 hash value");
		write_result(ctx, "Invalid client certificate SHA256 hash value in PPS");
		xml_node_get_text_free(ctx->xml, fingerprint);
		return -1;
	}
	xml_node_get_text_free(ctx->xml, fingerprint);

	der = os_readfile("Cert/est_cert.der", &der_len);
	if (der == NULL) {
		wpa_printf(MSG_INFO, "Could not find client certificate from EST");
		write_result(ctx, "Could not find client certificate from EST");
		return -1;
	}

	if (sha256_vector(1, (const u8 **) &der, &der_len, digest2) < 0) {
		os_free(der);
		return -1;
	}
	os_free(der);

	if (os_memcmp(digest1, digest2, sizeof(digest1)) != 0) {
		wpa_printf(MSG_INFO, "Client certificate from EST does not match fingerprint from PPS MO");
		write_result(ctx, "Client certificate from EST does not match fingerprint from PPS MO");
		return -1;
	}

	wpa_printf(MSG_INFO, "Client certificate from EST matches PPS MO");
	unlink("Cert/est_cert.der");

	os_snprintf(buf, sizeof(buf), "SP/%s/client-ca.pem", fqdn);
	if (rename("Cert/est-cacerts.pem", buf) < 0) {
		wpa_printf(MSG_INFO, "Could not move est-cacerts.pem to client-ca.pem: %s",
			   strerror(errno));
		return -1;
	}
	pem = os_readfile(buf, &pem_len);

	os_snprintf(buf, sizeof(buf), "SP/%s/client-cert.pem", fqdn);
	if (rename("Cert/est_cert.pem", buf) < 0) {
		wpa_printf(MSG_INFO, "Could not move est_cert.pem to client-cert.pem: %s",
			   strerror(errno));
		os_free(pem);
		return -1;
	}

	if (pem) {
		FILE *f = fopen(buf, "a");
		if (f) {
			fwrite(pem, pem_len, 1, f);
			fclose(f);
		}
		os_free(pem);
	}

	os_snprintf(buf, sizeof(buf), "SP/%s/client-key.pem", fqdn);
	if (rename("Cert/privkey-plain.pem", buf) < 0) {
		wpa_printf(MSG_INFO, "Could not move privkey-plain.pem to client-key.pem: %s",
			   strerror(errno));
		return -1;
	}

	unlink("Cert/est-req.b64");
	unlink("Cert/est-req.pem");
	rmdir("Cert");

	return 0;
}


#define TMP_CERT_DL_FILE "tmp-cert-download"

static int download_cert(struct hs20_osu_client *ctx, xml_node_t *params,
			 const char *fname)
{
	xml_node_t *url_node, *hash_node;
	char *url, *hash;
	char *cert;
	size_t len;
	u8 digest1[SHA256_MAC_LEN], digest2[SHA256_MAC_LEN];
	int res;
	unsigned char *b64;
	FILE *f;

	url_node = get_node(ctx->xml, params, "CertURL");
	hash_node = get_node(ctx->xml, params, "CertSHA256Fingerprint");
	if (url_node == NULL || hash_node == NULL)
		return -1;
	url = xml_node_get_text(ctx->xml, url_node);
	hash = xml_node_get_text(ctx->xml, hash_node);
	if (url == NULL || hash == NULL) {
		xml_node_get_text_free(ctx->xml, url);
		xml_node_get_text_free(ctx->xml, hash);
		return -1;
	}

	wpa_printf(MSG_INFO, "CertURL: %s", url);
	wpa_printf(MSG_INFO, "SHA256 hash: %s", hash);

	if (hexstr2bin(hash, digest1, SHA256_MAC_LEN) < 0) {
		wpa_printf(MSG_INFO, "Invalid SHA256 hash value");
		write_result(ctx, "Invalid SHA256 hash value for downloaded certificate");
		xml_node_get_text_free(ctx->xml, hash);
		return -1;
	}
	xml_node_get_text_free(ctx->xml, hash);

	write_summary(ctx, "Download certificate from %s", url);
	ctx->no_osu_cert_validation = 1;
	http_ocsp_set(ctx->http, 1);
	res = http_download_file(ctx->http, url, TMP_CERT_DL_FILE, NULL);
	http_ocsp_set(ctx->http,
		      (ctx->workarounds & WORKAROUND_OCSP_OPTIONAL) ? 1 : 2);
	ctx->no_osu_cert_validation = 0;
	xml_node_get_text_free(ctx->xml, url);
	if (res < 0)
		return -1;

	cert = os_readfile(TMP_CERT_DL_FILE, &len);
	remove(TMP_CERT_DL_FILE);
	if (cert == NULL)
		return -1;

	if (sha256_vector(1, (const u8 **) &cert, &len, digest2) < 0) {
		os_free(cert);
		return -1;
	}

	if (os_memcmp(digest1, digest2, sizeof(digest1)) != 0) {
		wpa_printf(MSG_INFO, "Downloaded certificate fingerprint did not match");
		write_result(ctx, "Downloaded certificate fingerprint did not match");
		os_free(cert);
		return -1;
	}

	b64 = base64_encode((unsigned char *) cert, len, NULL);
	os_free(cert);
	if (b64 == NULL)
		return -1;

	f = fopen(fname, "wb");
	if (f == NULL) {
		os_free(b64);
		return -1;
	}

	fprintf(f, "-----BEGIN CERTIFICATE-----\n"
		"%s"
		"-----END CERTIFICATE-----\n",
		b64);

	os_free(b64);
	fclose(f);

	wpa_printf(MSG_INFO, "Downloaded certificate into %s and validated fingerprint",
		   fname);
	write_summary(ctx, "Downloaded certificate into %s and validated fingerprint",
		      fname);

	return 0;
}


static int cmd_dl_osu_ca(struct hs20_osu_client *ctx, const char *pps_fname,
			 const char *ca_fname)
{
	xml_node_t *pps, *node;
	int ret;

	pps = node_from_file(ctx->xml, pps_fname);
	if (pps == NULL) {
		wpa_printf(MSG_INFO, "Could not read or parse '%s'", pps_fname);
		return -1;
	}

	node = get_child_node(ctx->xml, pps,
			      "SubscriptionUpdate/TrustRoot");
	if (node == NULL) {
		wpa_printf(MSG_INFO, "No SubscriptionUpdate/TrustRoot/CertURL found from PPS");
		xml_node_free(ctx->xml, pps);
		return -1;
	}

	ret = download_cert(ctx, node, ca_fname);
	xml_node_free(ctx->xml, pps);

	return ret;
}


static int cmd_dl_polupd_ca(struct hs20_osu_client *ctx, const char *pps_fname,
			    const char *ca_fname)
{
	xml_node_t *pps, *node;
	int ret;

	pps = node_from_file(ctx->xml, pps_fname);
	if (pps == NULL) {
		wpa_printf(MSG_INFO, "Could not read or parse '%s'", pps_fname);
		return -1;
	}

	node = get_child_node(ctx->xml, pps,
			      "Policy/PolicyUpdate/TrustRoot");
	if (node == NULL) {
		wpa_printf(MSG_INFO, "No Policy/PolicyUpdate/TrustRoot/CertURL found from PPS");
		xml_node_free(ctx->xml, pps);
		return -2;
	}

	ret = download_cert(ctx, node, ca_fname);
	xml_node_free(ctx->xml, pps);

	return ret;
}


static int cmd_dl_aaa_ca(struct hs20_osu_client *ctx, const char *pps_fname,
			 const char *ca_fname)
{
	xml_node_t *pps, *node, *aaa;
	int ret;

	pps = node_from_file(ctx->xml, pps_fname);
	if (pps == NULL) {
		wpa_printf(MSG_INFO, "Could not read or parse '%s'", pps_fname);
		return -1;
	}

	node = get_child_node(ctx->xml, pps,
			      "AAAServerTrustRoot");
	if (node == NULL) {
		wpa_printf(MSG_INFO, "No AAAServerTrustRoot/CertURL found from PPS");
		xml_node_free(ctx->xml, pps);
		return -2;
	}

	aaa = xml_node_first_child(ctx->xml, node);
	if (aaa == NULL) {
		wpa_printf(MSG_INFO, "No AAAServerTrustRoot/CertURL found from PPS");
		xml_node_free(ctx->xml, pps);
		return -1;
	}

	ret = download_cert(ctx, aaa, ca_fname);
	xml_node_free(ctx->xml, pps);

	return ret;
}


static int download_trust_roots(struct hs20_osu_client *ctx,
				const char *pps_fname)
{
	char *dir, *pos;
	char fname[300];
	int ret, ret1;

	dir = os_strdup(pps_fname);
	if (dir == NULL)
		return -1;
	pos = os_strrchr(dir, '/');
	if (pos == NULL) {
		os_free(dir);
		return -1;
	}
	*pos = '\0';

	snprintf(fname, sizeof(fname), "%s/ca.pem", dir);
	ret = cmd_dl_osu_ca(ctx, pps_fname, fname);
	snprintf(fname, sizeof(fname), "%s/polupd-ca.pem", dir);
	ret1 = cmd_dl_polupd_ca(ctx, pps_fname, fname);
	if (ret == 0 && ret1 == -1)
		ret = -1;
	snprintf(fname, sizeof(fname), "%s/aaa-ca.pem", dir);
	ret1 = cmd_dl_aaa_ca(ctx, pps_fname, fname);
	if (ret == 0 && ret1 == -1)
		ret = -1;

	os_free(dir);

	return ret;
}


static int server_dnsname_suffix_match(struct hs20_osu_client *ctx,
				       const char *fqdn)
{
	size_t match_len, len, i;
	const char *val;

	match_len = os_strlen(fqdn);

	for (i = 0; i < ctx->server_dnsname_count; i++) {
		wpa_printf(MSG_INFO,
			   "Checking suffix match against server dNSName %s",
			   ctx->server_dnsname[i]);
		val = ctx->server_dnsname[i];
		len = os_strlen(val);

		if (match_len > len)
			continue;

		if (os_strncasecmp(val + len - match_len, fqdn, match_len) != 0)
			continue; /* no match */

		if (match_len == len)
			return 1; /* exact match */

		if (val[len - match_len - 1] == '.')
			return 1; /* full label match completes suffix match */

		/* Reject due to incomplete label match */
	}

	/* None of the dNSName(s) matched */
	return 0;
}


int hs20_add_pps_mo(struct hs20_osu_client *ctx, const char *uri,
		    xml_node_t *add_mo, char *fname, size_t fname_len)
{
	char *str;
	char *fqdn, *pos;
	xml_node_t *tnds, *mo, *cert;
	const char *name;
	int ret;

	if (strncmp(uri, "./Wi-Fi/", 8) != 0) {
		wpa_printf(MSG_INFO, "Unsupported location for addMO to add PPS MO: '%s'",
			   uri);
		write_result(ctx, "Unsupported location for addMO to add PPS MO: '%s'",
			     uri);
		return -1;
	}

	fqdn = strdup(uri + 8);
	if (fqdn == NULL)
		return -1;
	pos = strchr(fqdn, '/');
	if (pos) {
		if (os_strcasecmp(pos, "/PerProviderSubscription") != 0) {
			wpa_printf(MSG_INFO, "Unsupported location for addMO to add PPS MO (extra directory): '%s'",
				   uri);
			write_result(ctx, "Unsupported location for addMO to "
				     "add PPS MO (extra directory): '%s'", uri);
			free(fqdn);
			return -1;
		}
		*pos = '\0'; /* remove trailing slash and PPS node name */
	}
	wpa_printf(MSG_INFO, "SP FQDN: %s", fqdn);

	if (!server_dnsname_suffix_match(ctx, fqdn)) {
		wpa_printf(MSG_INFO,
			   "FQDN '%s' for new PPS MO did not have suffix match with server's dNSName values, count: %d",
			   fqdn, (int) ctx->server_dnsname_count);
		write_result(ctx, "FQDN '%s' for new PPS MO did not have suffix match with server's dNSName values",
			     fqdn);
		free(fqdn);
		return -1;
	}

	if (!valid_fqdn(fqdn)) {
		wpa_printf(MSG_INFO, "Invalid FQDN '%s'", fqdn);
		write_result(ctx, "Invalid FQDN '%s'", fqdn);
		free(fqdn);
		return -1;
	}

	mkdir("SP", S_IRWXU);
	snprintf(fname, fname_len, "SP/%s", fqdn);
	if (mkdir(fname, S_IRWXU) < 0) {
		if (errno != EEXIST) {
			int err = errno;
			wpa_printf(MSG_INFO, "mkdir(%s) failed: %s",
				   fname, strerror(err));
			free(fqdn);
			return -1;
		}
	}

	android_update_permission("SP", S_IRWXU | S_IRGRP | S_IXGRP);
	android_update_permission(fname, S_IRWXU | S_IRGRP | S_IXGRP);

	snprintf(fname, fname_len, "SP/%s/pps.xml", fqdn);

	if (os_file_exists(fname)) {
		wpa_printf(MSG_INFO, "PPS file '%s' exists - reject addMO",
			   fname);
		write_result(ctx, "PPS file '%s' exists - reject addMO",
			     fname);
		free(fqdn);
		return -2;
	}
	wpa_printf(MSG_INFO, "Using PPS file: %s", fname);

	str = xml_node_get_text(ctx->xml, add_mo);
	if (str == NULL) {
		wpa_printf(MSG_INFO, "Could not extract MO text");
		free(fqdn);
		return -1;
	}
	wpa_printf(MSG_DEBUG, "[hs20] addMO text: '%s'", str);

	tnds = xml_node_from_buf(ctx->xml, str);
	xml_node_get_text_free(ctx->xml, str);
	if (tnds == NULL) {
		wpa_printf(MSG_INFO, "[hs20] Could not parse addMO text");
		free(fqdn);
		return -1;
	}

	mo = tnds_to_mo(ctx->xml, tnds);
	if (mo == NULL) {
		wpa_printf(MSG_INFO, "[hs20] Could not parse addMO TNDS text");
		free(fqdn);
		return -1;
	}

	debug_dump_node(ctx, "Parsed TNDS", mo);

	name = xml_node_get_localname(ctx->xml, mo);
	if (os_strcasecmp(name, "PerProviderSubscription") != 0) {
		wpa_printf(MSG_INFO, "[hs20] Unexpected PPS MO root node name '%s'",
			   name);
		free(fqdn);
		return -1;
	}

	cert = get_child_node(ctx->xml, mo,
			      "Credential/DigitalCertificate/"
			      "CertSHA256Fingerprint");
	if (cert && process_est_cert(ctx, cert, fqdn) < 0) {
		xml_node_free(ctx->xml, mo);
		free(fqdn);
		return -1;
	}
	free(fqdn);

	if (node_to_file(ctx->xml, fname, mo) < 0) {
		wpa_printf(MSG_INFO, "Could not write MO to file");
		xml_node_free(ctx->xml, mo);
		return -1;
	}
	xml_node_free(ctx->xml, mo);

	wpa_printf(MSG_INFO, "A new PPS MO added as '%s'", fname);
	write_summary(ctx, "A new PPS MO added as '%s'", fname);

	ret = download_trust_roots(ctx, fname);
	if (ret < 0) {
		wpa_printf(MSG_INFO, "Remove invalid PPS MO file");
		write_summary(ctx, "Remove invalid PPS MO file");
		unlink(fname);
	}

	return ret;
}


int update_pps_file(struct hs20_osu_client *ctx, const char *pps_fname,
		    xml_node_t *pps)
{
	char *str;
	FILE *f;
	char backup[300];

	if (ctx->client_cert_present) {
		xml_node_t *cert;
		cert = get_child_node(ctx->xml, pps,
				      "Credential/DigitalCertificate/"
				      "CertSHA256Fingerprint");
		if (cert && os_file_exists("Cert/est_cert.der") &&
		    process_est_cert(ctx, cert, ctx->fqdn) < 0) {
			wpa_printf(MSG_INFO, "EST certificate update processing failed on PPS MO update");
			return -1;
		}
	}

	wpa_printf(MSG_INFO, "Updating PPS MO %s", pps_fname);

	str = xml_node_to_str(ctx->xml, pps);
	if (str == NULL) {
		wpa_printf(MSG_ERROR, "No node found");
		return -1;
	}
	wpa_printf(MSG_MSGDUMP, "[hs20] Updated PPS: '%s'", str);

	snprintf(backup, sizeof(backup), "%s.bak", pps_fname);
	rename(pps_fname, backup);
	f = fopen(pps_fname, "w");
	if (f == NULL) {
		wpa_printf(MSG_INFO, "Could not write PPS");
		rename(backup, pps_fname);
		free(str);
		return -1;
	}
	fprintf(f, "%s\n", str);
	fclose(f);

	free(str);

	return 0;
}


void get_user_pw(struct hs20_osu_client *ctx, xml_node_t *pps,
		 const char *alt_loc, char **user, char **pw)
{
	xml_node_t *node;

	node = get_child_node(ctx->xml, pps,
			      "Credential/UsernamePassword/Username");
	if (node)
		*user = xml_node_get_text(ctx->xml, node);

	node = get_child_node(ctx->xml, pps,
			      "Credential/UsernamePassword/Password");
	if (node)
		*pw = xml_node_get_base64_text(ctx->xml, node, NULL);

	node = get_child_node(ctx->xml, pps, alt_loc);
	if (node) {
		xml_node_t *a;
		a = get_node(ctx->xml, node, "Username");
		if (a) {
			xml_node_get_text_free(ctx->xml, *user);
			*user = xml_node_get_text(ctx->xml, a);
			wpa_printf(MSG_INFO, "Use OSU username '%s'", *user);
		}

		a = get_node(ctx->xml, node, "Password");
		if (a) {
			free(*pw);
			*pw = xml_node_get_base64_text(ctx->xml, a, NULL);
			wpa_printf(MSG_INFO, "Use OSU password");
		}
	}
}


/* Remove old credentials based on HomeSP/FQDN */
static void remove_sp_creds(struct hs20_osu_client *ctx, const char *fqdn)
{
	char cmd[300];
	os_snprintf(cmd, sizeof(cmd), "REMOVE_CRED provisioning_sp=%s", fqdn);
	if (wpa_command(ctx->ifname, cmd) < 0)
		wpa_printf(MSG_INFO, "Failed to remove old credential(s)");
}


static void set_pps_cred_policy_spe(struct hs20_osu_client *ctx, int id,
				    xml_node_t *spe)
{
	xml_node_t *ssid;
	char *txt;

	ssid = get_node(ctx->xml, spe, "SSID");
	if (ssid == NULL)
		return;
	txt = xml_node_get_text(ctx->xml, ssid);
	if (txt == NULL)
		return;
	wpa_printf(MSG_DEBUG, "- Policy/SPExclusionList/<X+>/SSID = %s", txt);
	if (set_cred_quoted(ctx->ifname, id, "excluded_ssid", txt) < 0)
		wpa_printf(MSG_INFO, "Failed to set cred excluded_ssid");
	xml_node_get_text_free(ctx->xml, txt);
}


static void set_pps_cred_policy_spel(struct hs20_osu_client *ctx, int id,
				     xml_node_t *spel)
{
	xml_node_t *child;

	xml_node_for_each_child(ctx->xml, child, spel) {
		xml_node_for_each_check(ctx->xml, child);
		set_pps_cred_policy_spe(ctx, id, child);
	}
}


static void set_pps_cred_policy_prp(struct hs20_osu_client *ctx, int id,
				    xml_node_t *prp)
{
	xml_node_t *node;
	char *txt = NULL, *pos;
	char *prio, *country_buf = NULL;
	const char *country;
	char val[200];
	int priority;

	node = get_node(ctx->xml, prp, "Priority");
	if (node == NULL)
		return;
	prio = xml_node_get_text(ctx->xml, node);
	if (prio == NULL)
		return;
	wpa_printf(MSG_INFO, "- Policy/PreferredRoamingPartnerList/<X+>/Priority = %s",
		   prio);
	priority = atoi(prio);
	xml_node_get_text_free(ctx->xml, prio);

	node = get_node(ctx->xml, prp, "Country");
	if (node) {
		country_buf = xml_node_get_text(ctx->xml, node);
		if (country_buf == NULL)
			return;
		country = country_buf;
		wpa_printf(MSG_INFO, "- Policy/PreferredRoamingPartnerList/<X+>/Country = %s",
			   country);
	} else {
		country = "*";
	}

	node = get_node(ctx->xml, prp, "FQDN_Match");
	if (node == NULL)
		goto out;
	txt = xml_node_get_text(ctx->xml, node);
	if (txt == NULL)
		goto out;
	wpa_printf(MSG_INFO, "- Policy/PreferredRoamingPartnerList/<X+>/FQDN_Match = %s",
		   txt);
	pos = strrchr(txt, ',');
	if (pos == NULL)
		goto out;
	*pos++ = '\0';

	snprintf(val, sizeof(val), "%s,%d,%d,%s", txt,
		 strcmp(pos, "includeSubdomains") != 0, priority, country);
	if (set_cred_quoted(ctx->ifname, id, "roaming_partner", val) < 0)
		wpa_printf(MSG_INFO, "Failed to set cred roaming_partner");
out:
	xml_node_get_text_free(ctx->xml, country_buf);
	xml_node_get_text_free(ctx->xml, txt);
}


static void set_pps_cred_policy_prpl(struct hs20_osu_client *ctx, int id,
				     xml_node_t *prpl)
{
	xml_node_t *child;

	xml_node_for_each_child(ctx->xml, child, prpl) {
		xml_node_for_each_check(ctx->xml, child);
		set_pps_cred_policy_prp(ctx, id, child);
	}
}


static void set_pps_cred_policy_min_backhaul(struct hs20_osu_client *ctx, int id,
					     xml_node_t *min_backhaul)
{
	xml_node_t *node;
	char *type, *dl = NULL, *ul = NULL;
	int home;

	node = get_node(ctx->xml, min_backhaul, "NetworkType");
	if (node == NULL) {
		wpa_printf(MSG_INFO, "Ignore MinBackhaulThreshold without mandatory NetworkType node");
		return;
	}

	type = xml_node_get_text(ctx->xml, node);
	if (type == NULL)
		return;
	wpa_printf(MSG_INFO, "- Policy/MinBackhaulThreshold/<X+>/NetworkType = %s",
		   type);
	if (os_strcasecmp(type, "home") == 0)
		home = 1;
	else if (os_strcasecmp(type, "roaming") == 0)
		home = 0;
	else {
		wpa_printf(MSG_INFO, "Ignore MinBackhaulThreshold with invalid NetworkType");
		xml_node_get_text_free(ctx->xml, type);
		return;
	}
	xml_node_get_text_free(ctx->xml, type);

	node = get_node(ctx->xml, min_backhaul, "DLBandwidth");
	if (node)
		dl = xml_node_get_text(ctx->xml, node);

	node = get_node(ctx->xml, min_backhaul, "ULBandwidth");
	if (node)
		ul = xml_node_get_text(ctx->xml, node);

	if (dl == NULL && ul == NULL) {
		wpa_printf(MSG_INFO, "Ignore MinBackhaulThreshold without either DLBandwidth or ULBandwidth nodes");
		return;
	}

	if (dl)
		wpa_printf(MSG_INFO, "- Policy/MinBackhaulThreshold/<X+>/DLBandwidth = %s",
			   dl);
	if (ul)
		wpa_printf(MSG_INFO, "- Policy/MinBackhaulThreshold/<X+>/ULBandwidth = %s",
			   ul);

	if (home) {
		if (dl &&
		    set_cred(ctx->ifname, id, "min_dl_bandwidth_home", dl) < 0)
			wpa_printf(MSG_INFO, "Failed to set cred bandwidth limit");
		if (ul &&
		    set_cred(ctx->ifname, id, "min_ul_bandwidth_home", ul) < 0)
			wpa_printf(MSG_INFO, "Failed to set cred bandwidth limit");
	} else {
		if (dl &&
		    set_cred(ctx->ifname, id, "min_dl_bandwidth_roaming", dl) <
		    0)
			wpa_printf(MSG_INFO, "Failed to set cred bandwidth limit");
		if (ul &&
		    set_cred(ctx->ifname, id, "min_ul_bandwidth_roaming", ul) <
		    0)
			wpa_printf(MSG_INFO, "Failed to set cred bandwidth limit");
	}

	xml_node_get_text_free(ctx->xml, dl);
	xml_node_get_text_free(ctx->xml, ul);
}


static void set_pps_cred_policy_min_backhaul_list(struct hs20_osu_client *ctx,
						  int id, xml_node_t *node)
{
	xml_node_t *child;

	wpa_printf(MSG_INFO, "- Policy/MinBackhaulThreshold");

	xml_node_for_each_child(ctx->xml, child, node) {
		xml_node_for_each_check(ctx->xml, child);
		set_pps_cred_policy_min_backhaul(ctx, id, child);
	}
}


static void set_pps_cred_policy_update(struct hs20_osu_client *ctx, int id,
				       xml_node_t *node)
{
	wpa_printf(MSG_INFO, "- Policy/PolicyUpdate");
	/* Not used in wpa_supplicant */
}


static void set_pps_cred_policy_required_proto_port(struct hs20_osu_client *ctx,
						    int id, xml_node_t *tuple)
{
	xml_node_t *node;
	char *proto, *port;
	char *buf;
	size_t buflen;

	node = get_node(ctx->xml, tuple, "IPProtocol");
	if (node == NULL) {
		wpa_printf(MSG_INFO, "Ignore RequiredProtoPortTuple without mandatory IPProtocol node");
		return;
	}

	proto = xml_node_get_text(ctx->xml, node);
	if (proto == NULL)
		return;

	wpa_printf(MSG_INFO, "- Policy/RequiredProtoPortTuple/<X+>/IPProtocol = %s",
		   proto);

	node = get_node(ctx->xml, tuple, "PortNumber");
	port = node ? xml_node_get_text(ctx->xml, node) : NULL;
	if (port) {
		wpa_printf(MSG_INFO, "- Policy/RequiredProtoPortTuple/<X+>/PortNumber = %s",
			   port);
		buflen = os_strlen(proto) + os_strlen(port) + 10;
		buf = os_malloc(buflen);
		if (buf)
			os_snprintf(buf, buflen, "%s:%s", proto, port);
		xml_node_get_text_free(ctx->xml, port);
	} else {
		buflen = os_strlen(proto) + 10;
		buf = os_malloc(buflen);
		if (buf)
			os_snprintf(buf, buflen, "%s", proto);
	}

	xml_node_get_text_free(ctx->xml, proto);

	if (buf == NULL)
		return;

	if (set_cred(ctx->ifname, id, "req_conn_capab", buf) < 0)
		wpa_printf(MSG_INFO, "Could not set req_conn_capab");

	os_free(buf);
}


static void set_pps_cred_policy_required_proto_ports(struct hs20_osu_client *ctx,
						     int id, xml_node_t *node)
{
	xml_node_t *child;

	wpa_printf(MSG_INFO, "- Policy/RequiredProtoPortTuple");

	xml_node_for_each_child(ctx->xml, child, node) {
		xml_node_for_each_check(ctx->xml, child);
		set_pps_cred_policy_required_proto_port(ctx, id, child);
	}
}


static void set_pps_cred_policy_max_bss_load(struct hs20_osu_client *ctx, int id,
					     xml_node_t *node)
{
	char *str = xml_node_get_text(ctx->xml, node);
	if (str == NULL)
		return;
	wpa_printf(MSG_INFO, "- Policy/MaximumBSSLoadValue - %s", str);
	if (set_cred(ctx->ifname, id, "max_bss_load", str) < 0)
		wpa_printf(MSG_INFO, "Failed to set cred max_bss_load limit");
	xml_node_get_text_free(ctx->xml, str);
}


static void set_pps_cred_policy(struct hs20_osu_client *ctx, int id,
				xml_node_t *node)
{
	xml_node_t *child;
	const char *name;

	wpa_printf(MSG_INFO, "- Policy");

	xml_node_for_each_child(ctx->xml, child, node) {
		xml_node_for_each_check(ctx->xml, child);
		name = xml_node_get_localname(ctx->xml, child);
		if (os_strcasecmp(name, "PreferredRoamingPartnerList") == 0)
			set_pps_cred_policy_prpl(ctx, id, child);
		else if (os_strcasecmp(name, "MinBackhaulThreshold") == 0)
			set_pps_cred_policy_min_backhaul_list(ctx, id, child);
		else if (os_strcasecmp(name, "PolicyUpdate") == 0)
			set_pps_cred_policy_update(ctx, id, child);
		else if (os_strcasecmp(name, "SPExclusionList") == 0)
			set_pps_cred_policy_spel(ctx, id, child);
		else if (os_strcasecmp(name, "RequiredProtoPortTuple") == 0)
			set_pps_cred_policy_required_proto_ports(ctx, id, child);
		else if (os_strcasecmp(name, "MaximumBSSLoadValue") == 0)
			set_pps_cred_policy_max_bss_load(ctx, id, child);
		else
			wpa_printf(MSG_INFO, "Unknown Policy node '%s'", name);
	}
}


static void set_pps_cred_priority(struct hs20_osu_client *ctx, int id,
				  xml_node_t *node)
{
	char *str = xml_node_get_text(ctx->xml, node);
	if (str == NULL)
		return;
	wpa_printf(MSG_INFO, "- CredentialPriority = %s", str);
	if (set_cred(ctx->ifname, id, "sp_priority", str) < 0)
		wpa_printf(MSG_INFO, "Failed to set cred sp_priority");
	xml_node_get_text_free(ctx->xml, str);
}


static void set_pps_cred_aaa_server_trust_root(struct hs20_osu_client *ctx,
					       int id, xml_node_t *node)
{
	wpa_printf(MSG_INFO, "- AAAServerTrustRoot - TODO");
}


static void set_pps_cred_sub_update(struct hs20_osu_client *ctx, int id,
				    xml_node_t *node)
{
	wpa_printf(MSG_INFO, "- SubscriptionUpdate");
	/* not used within wpa_supplicant */
}


static void set_pps_cred_home_sp_network_id(struct hs20_osu_client *ctx,
					    int id, xml_node_t *node)
{
	xml_node_t *ssid_node, *hessid_node;
	char *ssid, *hessid;

	ssid_node = get_node(ctx->xml, node, "SSID");
	if (ssid_node == NULL) {
		wpa_printf(MSG_INFO, "Ignore HomeSP/NetworkID without mandatory SSID node");
		return;
	}

	hessid_node = get_node(ctx->xml, node, "HESSID");

	ssid = xml_node_get_text(ctx->xml, ssid_node);
	if (ssid == NULL)
		return;
	hessid = hessid_node ? xml_node_get_text(ctx->xml, hessid_node) : NULL;

	wpa_printf(MSG_INFO, "- HomeSP/NetworkID/<X+>/SSID = %s", ssid);
	if (hessid)
		wpa_printf(MSG_INFO, "- HomeSP/NetworkID/<X+>/HESSID = %s",
			   hessid);

	/* TODO: Configure to wpa_supplicant */

	xml_node_get_text_free(ctx->xml, ssid);
	xml_node_get_text_free(ctx->xml, hessid);
}


static void set_pps_cred_home_sp_network_ids(struct hs20_osu_client *ctx,
					     int id, xml_node_t *node)
{
	xml_node_t *child;

	wpa_printf(MSG_INFO, "- HomeSP/NetworkID");

	xml_node_for_each_child(ctx->xml, child, node) {
		xml_node_for_each_check(ctx->xml, child);
		set_pps_cred_home_sp_network_id(ctx, id, child);
	}
}


static void set_pps_cred_home_sp_friendly_name(struct hs20_osu_client *ctx,
					       int id, xml_node_t *node)
{
	char *str = xml_node_get_text(ctx->xml, node);
	if (str == NULL)
		return;
	wpa_printf(MSG_INFO, "- HomeSP/FriendlyName = %s", str);
	/* not used within wpa_supplicant(?) */
	xml_node_get_text_free(ctx->xml, str);
}


static void set_pps_cred_home_sp_icon_url(struct hs20_osu_client *ctx,
					  int id, xml_node_t *node)
{
	char *str = xml_node_get_text(ctx->xml, node);
	if (str == NULL)
		return;
	wpa_printf(MSG_INFO, "- HomeSP/IconURL = %s", str);
	/* not used within wpa_supplicant */
	xml_node_get_text_free(ctx->xml, str);
}


static void set_pps_cred_home_sp_fqdn(struct hs20_osu_client *ctx, int id,
				      xml_node_t *node)
{
	char *str = xml_node_get_text(ctx->xml, node);
	if (str == NULL)
		return;
	wpa_printf(MSG_INFO, "- HomeSP/FQDN = %s", str);
	if (set_cred_quoted(ctx->ifname, id, "domain", str) < 0)
		wpa_printf(MSG_INFO, "Failed to set cred domain");
	if (set_cred_quoted(ctx->ifname, id, "domain_suffix_match", str) < 0)
		wpa_printf(MSG_INFO, "Failed to set cred domain_suffix_match");
	xml_node_get_text_free(ctx->xml, str);
}


static void set_pps_cred_home_sp_oi(struct hs20_osu_client *ctx, int id,
				    xml_node_t *node)
{
	xml_node_t *child;
	const char *name;
	char *homeoi = NULL;
	int required = 0;
	char *str;

	xml_node_for_each_child(ctx->xml, child, node) {
		xml_node_for_each_check(ctx->xml, child);
		name = xml_node_get_localname(ctx->xml, child);
		if (strcasecmp(name, "HomeOI") == 0 && !homeoi) {
			homeoi = xml_node_get_text(ctx->xml, child);
			wpa_printf(MSG_INFO, "- HomeSP/HomeOIList/<X+>/HomeOI = %s",
				   homeoi);
		} else if (strcasecmp(name, "HomeOIRequired") == 0) {
			str = xml_node_get_text(ctx->xml, child);
			wpa_printf(MSG_INFO, "- HomeSP/HomeOIList/<X+>/HomeOIRequired = '%s'",
				   str);
			if (str == NULL)
				continue;
			required = strcasecmp(str, "true") == 0;
			xml_node_get_text_free(ctx->xml, str);
		} else
			wpa_printf(MSG_INFO, "Unknown HomeOIList node '%s'",
				   name);
	}

	if (homeoi == NULL) {
		wpa_printf(MSG_INFO, "- HomeSP/HomeOIList/<X+> without HomeOI ignored");
		return;
	}

	wpa_printf(MSG_INFO, "- HomeSP/HomeOIList/<X+> '%s' required=%d",
		   homeoi, required);

	if (required) {
		if (set_cred(ctx->ifname, id, "required_roaming_consortium",
			     homeoi) < 0)
			wpa_printf(MSG_INFO, "Failed to set cred required_roaming_consortium");
	} else {
		if (set_cred(ctx->ifname, id, "roaming_consortium", homeoi) < 0)
			wpa_printf(MSG_INFO, "Failed to set cred roaming_consortium");
	}

	xml_node_get_text_free(ctx->xml, homeoi);
}


static void set_pps_cred_home_sp_oi_list(struct hs20_osu_client *ctx, int id,
					 xml_node_t *node)
{
	xml_node_t *child;

	wpa_printf(MSG_INFO, "- HomeSP/HomeOIList");

	xml_node_for_each_child(ctx->xml, child, node) {
		xml_node_for_each_check(ctx->xml, child);
		set_pps_cred_home_sp_oi(ctx, id, child);
	}
}


static void set_pps_cred_home_sp_other_partner(struct hs20_osu_client *ctx,
					       int id, xml_node_t *node)
{
	xml_node_t *child;
	const char *name;
	char *fqdn = NULL;

	xml_node_for_each_child(ctx->xml, child, node) {
		xml_node_for_each_check(ctx->xml, child);
		name = xml_node_get_localname(ctx->xml, child);
		if (os_strcasecmp(name, "FQDN") == 0 && !fqdn) {
			fqdn = xml_node_get_text(ctx->xml, child);
			wpa_printf(MSG_INFO, "- HomeSP/OtherHomePartners/<X+>/FQDN = %s",
				   fqdn);
		} else
			wpa_printf(MSG_INFO, "Unknown OtherHomePartners node '%s'",
				   name);
	}

	if (fqdn == NULL) {
		wpa_printf(MSG_INFO, "- HomeSP/OtherHomePartners/<X+> without FQDN ignored");
		return;
	}

	if (set_cred_quoted(ctx->ifname, id, "domain", fqdn) < 0)
		wpa_printf(MSG_INFO, "Failed to set cred domain for OtherHomePartners node");

	xml_node_get_text_free(ctx->xml, fqdn);
}


static void set_pps_cred_home_sp_other_partners(struct hs20_osu_client *ctx,
						int id,
						xml_node_t *node)
{
	xml_node_t *child;

	wpa_printf(MSG_INFO, "- HomeSP/OtherHomePartners");

	xml_node_for_each_child(ctx->xml, child, node) {
		xml_node_for_each_check(ctx->xml, child);
		set_pps_cred_home_sp_other_partner(ctx, id, child);
	}
}


static void set_pps_cred_home_sp_roaming_consortium_oi(
	struct hs20_osu_client *ctx, int id, xml_node_t *node)
{
	char *str = xml_node_get_text(ctx->xml, node);
	if (str == NULL)
		return;
	wpa_printf(MSG_INFO, "- HomeSP/RoamingConsortiumOI = %s", str);
	if (set_cred_quoted(ctx->ifname, id, "roaming_consortiums",
			    str) < 0)
		wpa_printf(MSG_INFO, "Failed to set cred roaming_consortiums");
	xml_node_get_text_free(ctx->xml, str);
}


static void set_pps_cred_home_sp(struct hs20_osu_client *ctx, int id,
				 xml_node_t *node)
{
	xml_node_t *child;
	const char *name;

	wpa_printf(MSG_INFO, "- HomeSP");

	xml_node_for_each_child(ctx->xml, child, node) {
		xml_node_for_each_check(ctx->xml, child);
		name = xml_node_get_localname(ctx->xml, child);
		if (os_strcasecmp(name, "NetworkID") == 0)
			set_pps_cred_home_sp_network_ids(ctx, id, child);
		else if (os_strcasecmp(name, "FriendlyName") == 0)
			set_pps_cred_home_sp_friendly_name(ctx, id, child);
		else if (os_strcasecmp(name, "IconURL") == 0)
			set_pps_cred_home_sp_icon_url(ctx, id, child);
		else if (os_strcasecmp(name, "FQDN") == 0)
			set_pps_cred_home_sp_fqdn(ctx, id, child);
		else if (os_strcasecmp(name, "HomeOIList") == 0)
			set_pps_cred_home_sp_oi_list(ctx, id, child);
		else if (os_strcasecmp(name, "OtherHomePartners") == 0)
			set_pps_cred_home_sp_other_partners(ctx, id, child);
		else if (os_strcasecmp(name, "RoamingConsortiumOI") == 0)
			set_pps_cred_home_sp_roaming_consortium_oi(ctx, id,
								   child);
		else
			wpa_printf(MSG_INFO, "Unknown HomeSP node '%s'", name);
	}
}


static void set_pps_cred_sub_params(struct hs20_osu_client *ctx, int id,
				    xml_node_t *node)
{
	wpa_printf(MSG_INFO, "- SubscriptionParameters");
	/* not used within wpa_supplicant */
}


static void set_pps_cred_creation_date(struct hs20_osu_client *ctx, int id,
				       xml_node_t *node)
{
	char *str = xml_node_get_text(ctx->xml, node);
	if (str == NULL)
		return;
	wpa_printf(MSG_INFO, "- Credential/CreationDate = %s", str);
	/* not used within wpa_supplicant */
	xml_node_get_text_free(ctx->xml, str);
}


static void set_pps_cred_expiration_date(struct hs20_osu_client *ctx, int id,
					 xml_node_t *node)
{
	char *str = xml_node_get_text(ctx->xml, node);
	if (str == NULL)
		return;
	wpa_printf(MSG_INFO, "- Credential/ExpirationDate = %s", str);
	/* not used within wpa_supplicant */
	xml_node_get_text_free(ctx->xml, str);
}


static void set_pps_cred_username(struct hs20_osu_client *ctx, int id,
				  xml_node_t *node)
{
	char *str = xml_node_get_text(ctx->xml, node);
	if (str == NULL)
		return;
	wpa_printf(MSG_INFO, "- Credential/UsernamePassword/Username = %s",
		   str);
	if (set_cred_quoted(ctx->ifname, id, "username", str) < 0)
		wpa_printf(MSG_INFO, "Failed to set cred username");
	xml_node_get_text_free(ctx->xml, str);
}


static void set_pps_cred_password(struct hs20_osu_client *ctx, int id,
				  xml_node_t *node)
{
	int len, i;
	char *pw, *hex, *pos, *end;

	pw = xml_node_get_base64_text(ctx->xml, node, &len);
	if (pw == NULL)
		return;

	wpa_printf(MSG_INFO, "- Credential/UsernamePassword/Password = %s", pw);

	hex = malloc(len * 2 + 1);
	if (hex == NULL) {
		free(pw);
		return;
	}
	end = hex + len * 2 + 1;
	pos = hex;
	for (i = 0; i < len; i++) {
		snprintf(pos, end - pos, "%02x", pw[i]);
		pos += 2;
	}
	free(pw);

	if (set_cred(ctx->ifname, id, "password", hex) < 0)
		wpa_printf(MSG_INFO, "Failed to set cred password");
	free(hex);
}


static void set_pps_cred_machine_managed(struct hs20_osu_client *ctx, int id,
					 xml_node_t *node)
{
	char *str = xml_node_get_text(ctx->xml, node);
	if (str == NULL)
		return;
	wpa_printf(MSG_INFO, "- Credential/UsernamePassword/MachineManaged = %s",
		   str);
	/* not used within wpa_supplicant */
	xml_node_get_text_free(ctx->xml, str);
}


static void set_pps_cred_soft_token_app(struct hs20_osu_client *ctx, int id,
					xml_node_t *node)
{
	char *str = xml_node_get_text(ctx->xml, node);
	if (str == NULL)
		return;
	wpa_printf(MSG_INFO, "- Credential/UsernamePassword/SoftTokenApp = %s",
		   str);
	/* not used within wpa_supplicant */
	xml_node_get_text_free(ctx->xml, str);
}


static void set_pps_cred_able_to_share(struct hs20_osu_client *ctx, int id,
				       xml_node_t *node)
{
	char *str = xml_node_get_text(ctx->xml, node);
	if (str == NULL)
		return;
	wpa_printf(MSG_INFO, "- Credential/UsernamePassword/AbleToShare = %s",
		   str);
	/* not used within wpa_supplicant */
	xml_node_get_text_free(ctx->xml, str);
}


static void set_pps_cred_eap_method_eap_type(struct hs20_osu_client *ctx,
					     int id, xml_node_t *node)
{
	char *str = xml_node_get_text(ctx->xml, node);
	int type;
	const char *eap_method = NULL;

	if (!str)
		return;
	wpa_printf(MSG_INFO,
		   "- Credential/UsernamePassword/EAPMethod/EAPType = %s", str);
	type = atoi(str);
	switch (type) {
	case EAP_TYPE_TLS:
		eap_method = "TLS";
		break;
	case EAP_TYPE_TTLS:
		eap_method = "TTLS";
		break;
	case EAP_TYPE_PEAP:
		eap_method = "PEAP";
		break;
	case EAP_TYPE_PWD:
		eap_method = "PWD";
		break;
	}
	xml_node_get_text_free(ctx->xml, str);
	if (!eap_method) {
		wpa_printf(MSG_INFO, "Unknown EAPType value");
		return;
	}

	if (set_cred(ctx->ifname, id, "eap", eap_method) < 0)
		wpa_printf(MSG_INFO, "Failed to set cred eap");
}


static void set_pps_cred_eap_method_inner_method(struct hs20_osu_client *ctx,
						 int id, xml_node_t *node)
{
	char *str = xml_node_get_text(ctx->xml, node);
	const char *phase2 = NULL;

	if (!str)
		return;
	wpa_printf(MSG_INFO,
		   "- Credential/UsernamePassword/EAPMethod/InnerMethod = %s",
		   str);
	if (os_strcmp(str, "PAP") == 0)
		phase2 = "auth=PAP";
	else if (os_strcmp(str, "CHAP") == 0)
		phase2 = "auth=CHAP";
	else if (os_strcmp(str, "MS-CHAP") == 0)
		phase2 = "auth=MSCHAP";
	else if (os_strcmp(str, "MS-CHAP-V2") == 0)
		phase2 = "auth=MSCHAPV2";
	xml_node_get_text_free(ctx->xml, str);
	if (!phase2) {
		wpa_printf(MSG_INFO, "Unknown InnerMethod value");
		return;
	}

	if (set_cred_quoted(ctx->ifname, id, "phase2", phase2) < 0)
		wpa_printf(MSG_INFO, "Failed to set cred phase2");
}


static void set_pps_cred_eap_method(struct hs20_osu_client *ctx, int id,
				    xml_node_t *node)
{
	xml_node_t *child;
	const char *name;

	wpa_printf(MSG_INFO, "- Credential/UsernamePassword/EAPMethod");

	xml_node_for_each_child(ctx->xml, child, node) {
		xml_node_for_each_check(ctx->xml, child);
		name = xml_node_get_localname(ctx->xml, child);
		if (os_strcasecmp(name, "EAPType") == 0)
			set_pps_cred_eap_method_eap_type(ctx, id, child);
		else if (os_strcasecmp(name, "InnerMethod") == 0)
			set_pps_cred_eap_method_inner_method(ctx, id, child);
		else
			wpa_printf(MSG_INFO, "Unknown Credential/UsernamePassword/EAPMethod node '%s'",
				   name);
	}
}


static void set_pps_cred_username_password(struct hs20_osu_client *ctx, int id,
					   xml_node_t *node)
{
	xml_node_t *child;
	const char *name;

	wpa_printf(MSG_INFO, "- Credential/UsernamePassword");

	xml_node_for_each_child(ctx->xml, child, node) {
		xml_node_for_each_check(ctx->xml, child);
		name = xml_node_get_localname(ctx->xml, child);
		if (os_strcasecmp(name, "Username") == 0)
			set_pps_cred_username(ctx, id, child);
		else if (os_strcasecmp(name, "Password") == 0)
			set_pps_cred_password(ctx, id, child);
		else if (os_strcasecmp(name, "MachineManaged") == 0)
			set_pps_cred_machine_managed(ctx, id, child);
		else if (os_strcasecmp(name, "SoftTokenApp") == 0)
			set_pps_cred_soft_token_app(ctx, id, child);
		else if (os_strcasecmp(name, "AbleToShare") == 0)
			set_pps_cred_able_to_share(ctx, id, child);
		else if (os_strcasecmp(name, "EAPMethod") == 0)
			set_pps_cred_eap_method(ctx, id, child);
		else
			wpa_printf(MSG_INFO, "Unknown Credential/UsernamePassword node '%s'",
				   name);
	}
}


static void set_pps_cred_digital_cert(struct hs20_osu_client *ctx, int id,
				      xml_node_t *node, const char *fqdn)
{
	char buf[200], dir[200];

	wpa_printf(MSG_INFO, "- Credential/DigitalCertificate");

	if (getcwd(dir, sizeof(dir)) == NULL)
		return;

	/* TODO: could build username from Subject of Subject AltName */
	if (set_cred_quoted(ctx->ifname, id, "username", "cert") < 0) {
		wpa_printf(MSG_INFO, "Failed to set username");
	}

	snprintf(buf, sizeof(buf), "%s/SP/%s/client-cert.pem", dir, fqdn);
	if (os_file_exists(buf)) {
		if (set_cred_quoted(ctx->ifname, id, "client_cert", buf) < 0) {
			wpa_printf(MSG_INFO, "Failed to set client_cert");
		}
	}

	snprintf(buf, sizeof(buf), "%s/SP/%s/client-key.pem", dir, fqdn);
	if (os_file_exists(buf)) {
		if (set_cred_quoted(ctx->ifname, id, "private_key", buf) < 0) {
			wpa_printf(MSG_INFO, "Failed to set private_key");
		}
	}
}


static void set_pps_cred_realm(struct hs20_osu_client *ctx, int id,
			       xml_node_t *node, const char *fqdn, int sim)
{
	char *str = xml_node_get_text(ctx->xml, node);
	char buf[200], dir[200];

	if (str == NULL)
		return;

	wpa_printf(MSG_INFO, "- Credential/Realm = %s", str);
	if (set_cred_quoted(ctx->ifname, id, "realm", str) < 0)
		wpa_printf(MSG_INFO, "Failed to set cred realm");
	xml_node_get_text_free(ctx->xml, str);

	if (sim)
		return;

	if (getcwd(dir, sizeof(dir)) == NULL)
		return;
	snprintf(buf, sizeof(buf), "%s/SP/%s/aaa-ca.pem", dir, fqdn);
	if (os_file_exists(buf)) {
		if (set_cred_quoted(ctx->ifname, id, "ca_cert", buf) < 0) {
			wpa_printf(MSG_INFO, "Failed to set CA cert");
		}
	}
}


static void set_pps_cred_check_aaa_cert_status(struct hs20_osu_client *ctx,
					       int id, xml_node_t *node)
{
	char *str = xml_node_get_text(ctx->xml, node);

	if (str == NULL)
		return;

	wpa_printf(MSG_INFO, "- Credential/CheckAAAServerCertStatus = %s", str);
	if (os_strcasecmp(str, "true") == 0 &&
	    set_cred(ctx->ifname, id, "ocsp", "2") < 0)
		wpa_printf(MSG_INFO, "Failed to set cred ocsp");
	xml_node_get_text_free(ctx->xml, str);
}


static void set_pps_cred_sim(struct hs20_osu_client *ctx, int id,
			     xml_node_t *sim, xml_node_t *realm)
{
	xml_node_t *node;
	char *imsi, *eaptype, *str, buf[20];
	int type;
	int mnc_len = 3;
	size_t imsi_len;

	node = get_node(ctx->xml, sim, "EAPType");
	if (node == NULL) {
		wpa_printf(MSG_INFO, "No SIM/EAPType node in credential");
		return;
	}
	eaptype = xml_node_get_text(ctx->xml, node);
	if (eaptype == NULL) {
		wpa_printf(MSG_INFO, "Could not extract SIM/EAPType");
		return;
	}
	wpa_printf(MSG_INFO, " - Credential/SIM/EAPType = %s", eaptype);
	type = atoi(eaptype);
	xml_node_get_text_free(ctx->xml, eaptype);

	switch (type) {
	case EAP_TYPE_SIM:
		if (set_cred(ctx->ifname, id, "eap", "SIM") < 0)
			wpa_printf(MSG_INFO, "Could not set eap=SIM");
		break;
	case EAP_TYPE_AKA:
		if (set_cred(ctx->ifname, id, "eap", "AKA") < 0)
			wpa_printf(MSG_INFO, "Could not set eap=SIM");
		break;
	case EAP_TYPE_AKA_PRIME:
		if (set_cred(ctx->ifname, id, "eap", "AKA'") < 0)
			wpa_printf(MSG_INFO, "Could not set eap=SIM");
		break;
	default:
		wpa_printf(MSG_INFO, "Unsupported SIM/EAPType %d", type);
		return;
	}

	node = get_node(ctx->xml, sim, "IMSI");
	if (node == NULL) {
		wpa_printf(MSG_INFO, "No SIM/IMSI node in credential");
		return;
	}
	imsi = xml_node_get_text(ctx->xml, node);
	if (imsi == NULL) {
		wpa_printf(MSG_INFO, "Could not extract SIM/IMSI");
		return;
	}
	wpa_printf(MSG_INFO, " - Credential/SIM/IMSI = %s", imsi);
	imsi_len = os_strlen(imsi);
	if (imsi_len < 7 || imsi_len + 2 > sizeof(buf)) {
		wpa_printf(MSG_INFO, "Invalid IMSI length");
		xml_node_get_text_free(ctx->xml, imsi);
		return;
	}

	str = xml_node_get_text(ctx->xml, node);
	if (str) {
		char *pos;
		pos = os_strstr(str, "mnc");
		if (pos && os_strlen(pos) >= 6) {
			if (os_strncmp(imsi + 3, pos + 3, 3) == 0)
				mnc_len = 3;
			else if (os_strncmp(imsi + 3, pos + 4, 2) == 0)
				mnc_len = 2;
		}
		xml_node_get_text_free(ctx->xml, str);
	}

	os_memcpy(buf, imsi, 3 + mnc_len);
	buf[3 + mnc_len] = '-';
	os_strlcpy(buf + 3 + mnc_len + 1, imsi + 3 + mnc_len,
		   sizeof(buf) - 3 - mnc_len - 1);

	xml_node_get_text_free(ctx->xml, imsi);

	if (set_cred_quoted(ctx->ifname, id, "imsi", buf) < 0)
		wpa_printf(MSG_INFO, "Could not set IMSI");

	if (set_cred_quoted(ctx->ifname, id, "milenage",
			    "90dca4eda45b53cf0f12d7c9c3bc6a89:"
			    "cb9cccc4b9258e6dca4760379fb82581:000000000123") <
	    0)
		wpa_printf(MSG_INFO, "Could not set Milenage parameters");
}


static void set_pps_cred_credential(struct hs20_osu_client *ctx, int id,
				    xml_node_t *node, const char *fqdn)
{
	xml_node_t *child, *sim, *realm;
	const char *name;

	wpa_printf(MSG_INFO, "- Credential");

	sim = get_node(ctx->xml, node, "SIM");
	realm = get_node(ctx->xml, node, "Realm");

	xml_node_for_each_child(ctx->xml, child, node) {
		xml_node_for_each_check(ctx->xml, child);
		name = xml_node_get_localname(ctx->xml, child);
		if (os_strcasecmp(name, "CreationDate") == 0)
			set_pps_cred_creation_date(ctx, id, child);
		else if (os_strcasecmp(name, "ExpirationDate") == 0)
			set_pps_cred_expiration_date(ctx, id, child);
		else if (os_strcasecmp(name, "UsernamePassword") == 0)
			set_pps_cred_username_password(ctx, id, child);
		else if (os_strcasecmp(name, "DigitalCertificate") == 0)
			set_pps_cred_digital_cert(ctx, id, child, fqdn);
		else if (os_strcasecmp(name, "Realm") == 0)
			set_pps_cred_realm(ctx, id, child, fqdn, sim != NULL);
		else if (os_strcasecmp(name, "CheckAAAServerCertStatus") == 0)
			set_pps_cred_check_aaa_cert_status(ctx, id, child);
		else if (os_strcasecmp(name, "SIM") == 0)
			set_pps_cred_sim(ctx, id, child, realm);
		else
			wpa_printf(MSG_INFO, "Unknown Credential node '%s'",
				   name);
	}
}


static void set_pps_credential(struct hs20_osu_client *ctx, int id,
			       xml_node_t *cred, const char *fqdn)
{
	xml_node_t *child;
	const char *name;

	xml_node_for_each_child(ctx->xml, child, cred) {
		xml_node_for_each_check(ctx->xml, child);
		name = xml_node_get_localname(ctx->xml, child);
		if (os_strcasecmp(name, "Policy") == 0)
			set_pps_cred_policy(ctx, id, child);
		else if (os_strcasecmp(name, "CredentialPriority") == 0)
			set_pps_cred_priority(ctx, id, child);
		else if (os_strcasecmp(name, "AAAServerTrustRoot") == 0)
			set_pps_cred_aaa_server_trust_root(ctx, id, child);
		else if (os_strcasecmp(name, "SubscriptionUpdate") == 0)
			set_pps_cred_sub_update(ctx, id, child);
		else if (os_strcasecmp(name, "HomeSP") == 0)
			set_pps_cred_home_sp(ctx, id, child);
		else if (os_strcasecmp(name, "SubscriptionParameters") == 0)
			set_pps_cred_sub_params(ctx, id, child);
		else if (os_strcasecmp(name, "Credential") == 0)
			set_pps_cred_credential(ctx, id, child, fqdn);
		else
			wpa_printf(MSG_INFO, "Unknown credential node '%s'",
				   name);
	}
}


static void set_pps(struct hs20_osu_client *ctx, xml_node_t *pps,
		    const char *fqdn)
{
	xml_node_t *child;
	const char *name;
	int id;
	char *update_identifier = NULL;

	/*
	 * TODO: Could consider more complex mechanism that would remove
	 * credentials only if there are changes in the information sent to
	 * wpa_supplicant.
	 */
	remove_sp_creds(ctx, fqdn);

	xml_node_for_each_child(ctx->xml, child, pps) {
		xml_node_for_each_check(ctx->xml, child);
		name = xml_node_get_localname(ctx->xml, child);
		if (os_strcasecmp(name, "UpdateIdentifier") == 0) {
			update_identifier = xml_node_get_text(ctx->xml, child);
			if (update_identifier) {
				wpa_printf(MSG_INFO, "- UpdateIdentifier = %s",
					   update_identifier);
				break;
			}
		}
	}

	xml_node_for_each_child(ctx->xml, child, pps) {
		xml_node_for_each_check(ctx->xml, child);
		name = xml_node_get_localname(ctx->xml, child);
		if (os_strcasecmp(name, "UpdateIdentifier") == 0)
			continue;
		id = add_cred(ctx->ifname);
		if (id < 0) {
			wpa_printf(MSG_INFO, "Failed to add credential to wpa_supplicant");
			write_summary(ctx, "Failed to add credential to wpa_supplicant");
			break;
		}
		write_summary(ctx, "Add a credential to wpa_supplicant");
		if (update_identifier &&
		    set_cred(ctx->ifname, id, "update_identifier",
			     update_identifier) < 0)
			wpa_printf(MSG_INFO, "Failed to set update_identifier");
		if (set_cred_quoted(ctx->ifname, id, "provisioning_sp", fqdn) <
		    0)
			wpa_printf(MSG_INFO, "Failed to set provisioning_sp");
		wpa_printf(MSG_INFO, "credential localname: '%s'", name);
		set_pps_credential(ctx, id, child, fqdn);
		ctx->pps_cred_set = 1;
	}

	xml_node_get_text_free(ctx->xml, update_identifier);
}


void cmd_set_pps(struct hs20_osu_client *ctx, const char *pps_fname)
{
	xml_node_t *pps;
	const char *fqdn;
	char *fqdn_buf = NULL, *pos;

	pps = node_from_file(ctx->xml, pps_fname);
	if (pps == NULL) {
		wpa_printf(MSG_INFO, "Could not read or parse '%s'", pps_fname);
		return;
	}

	fqdn = os_strstr(pps_fname, "SP/");
	if (fqdn) {
		fqdn_buf = os_strdup(fqdn + 3);
		if (fqdn_buf == NULL)
			return;
		pos = os_strchr(fqdn_buf, '/');
		if (pos)
			*pos = '\0';
		fqdn = fqdn_buf;
	} else
		fqdn = "wi-fi.org";

	wpa_printf(MSG_INFO, "Set PPS MO info to wpa_supplicant - SP FQDN %s",
		   fqdn);
	set_pps(ctx, pps, fqdn);

	os_free(fqdn_buf);
	xml_node_free(ctx->xml, pps);
}


static int cmd_get_fqdn(struct hs20_osu_client *ctx, const char *pps_fname)
{
	xml_node_t *pps, *node;
	char *fqdn = NULL;

	pps = node_from_file(ctx->xml, pps_fname);
	if (pps == NULL) {
		wpa_printf(MSG_INFO, "Could not read or parse '%s'", pps_fname);
		return -1;
	}

	node = get_child_node(ctx->xml, pps, "HomeSP/FQDN");
	if (node)
		fqdn = xml_node_get_text(ctx->xml, node);

	xml_node_free(ctx->xml, pps);

	if (fqdn) {
		FILE *f = fopen("pps-fqdn", "w");
		if (f) {
			fprintf(f, "%s", fqdn);
			fclose(f);
		}
		xml_node_get_text_free(ctx->xml, fqdn);
		return 0;
	}

	xml_node_get_text_free(ctx->xml, fqdn);
	return -1;
}


static void cmd_to_tnds(struct hs20_osu_client *ctx, const char *in_fname,
			const char *out_fname, const char *urn, int use_path)
{
	xml_node_t *mo, *node;

	mo = node_from_file(ctx->xml, in_fname);
	if (mo == NULL) {
		wpa_printf(MSG_INFO, "Could not read or parse '%s'", in_fname);
		return;
	}

	node = mo_to_tnds(ctx->xml, mo, use_path, urn, NULL);
	if (node) {
		node_to_file(ctx->xml, out_fname, node);
		xml_node_free(ctx->xml, node);
	}

	xml_node_free(ctx->xml, mo);
}


static void cmd_from_tnds(struct hs20_osu_client *ctx, const char *in_fname,
			  const char *out_fname)
{
	xml_node_t *tnds, *mo;

	tnds = node_from_file(ctx->xml, in_fname);
	if (tnds == NULL) {
		wpa_printf(MSG_INFO, "Could not read or parse '%s'", in_fname);
		return;
	}

	mo = tnds_to_mo(ctx->xml, tnds);
	if (mo) {
		node_to_file(ctx->xml, out_fname, mo);
		xml_node_free(ctx->xml, mo);
	}

	xml_node_free(ctx->xml, tnds);
}


struct osu_icon {
	int id;
	char lang[4];
	char mime_type[256];
	char filename[256];
};

struct osu_data {
	char bssid[20];
	char url[256];
	unsigned int methods;
	char osu_ssid[33];
	char osu_ssid2[33];
	char osu_nai[256];
	char osu_nai2[256];
	struct osu_lang_text friendly_name[MAX_OSU_VALS];
	size_t friendly_name_count;
	struct osu_lang_text serv_desc[MAX_OSU_VALS];
	size_t serv_desc_count;
	struct osu_icon icon[MAX_OSU_VALS];
	size_t icon_count;
};


static struct osu_data * parse_osu_providers(const char *fname, size_t *count)
{
	FILE *f;
	char buf[1000];
	struct osu_data *osu = NULL, *last = NULL;
	size_t osu_count = 0;
	char *pos, *end;

	f = fopen(fname, "r");
	if (f == NULL) {
		wpa_printf(MSG_ERROR, "Could not open %s", fname);
		return NULL;
	}

	while (fgets(buf, sizeof(buf), f)) {
		pos = strchr(buf, '\n');
		if (pos)
			*pos = '\0';

		if (strncmp(buf, "OSU-PROVIDER ", 13) == 0) {
			last = realloc(osu, (osu_count + 1) * sizeof(*osu));
			if (last == NULL)
				break;
			osu = last;
			last = &osu[osu_count++];
			memset(last, 0, sizeof(*last));
			snprintf(last->bssid, sizeof(last->bssid), "%s",
				 buf + 13);
			continue;
		}
		if (!last)
			continue;

		if (strncmp(buf, "uri=", 4) == 0) {
			snprintf(last->url, sizeof(last->url), "%s", buf + 4);
			continue;
		}

		if (strncmp(buf, "methods=", 8) == 0) {
			last->methods = strtol(buf + 8, NULL, 16);
			continue;
		}

		if (strncmp(buf, "osu_ssid=", 9) == 0) {
			snprintf(last->osu_ssid, sizeof(last->osu_ssid),
				 "%s", buf + 9);
			continue;
		}

		if (strncmp(buf, "osu_ssid2=", 10) == 0) {
			snprintf(last->osu_ssid2, sizeof(last->osu_ssid2),
				 "%s", buf + 10);
			continue;
		}

		if (os_strncmp(buf, "osu_nai=", 8) == 0) {
			os_snprintf(last->osu_nai, sizeof(last->osu_nai),
				    "%s", buf + 8);
			continue;
		}

		if (os_strncmp(buf, "osu_nai2=", 9) == 0) {
			os_snprintf(last->osu_nai2, sizeof(last->osu_nai2),
				    "%s", buf + 9);
			continue;
		}

		if (strncmp(buf, "friendly_name=", 14) == 0) {
			struct osu_lang_text *txt;
			if (last->friendly_name_count == MAX_OSU_VALS)
				continue;
			pos = strchr(buf + 14, ':');
			if (pos == NULL)
				continue;
			*pos++ = '\0';
			txt = &last->friendly_name[last->friendly_name_count++];
			snprintf(txt->lang, sizeof(txt->lang), "%s", buf + 14);
			snprintf(txt->text, sizeof(txt->text), "%s", pos);
		}

		if (strncmp(buf, "desc=", 5) == 0) {
			struct osu_lang_text *txt;
			if (last->serv_desc_count == MAX_OSU_VALS)
				continue;
			pos = strchr(buf + 5, ':');
			if (pos == NULL)
				continue;
			*pos++ = '\0';
			txt = &last->serv_desc[last->serv_desc_count++];
			snprintf(txt->lang, sizeof(txt->lang), "%s", buf + 5);
			snprintf(txt->text, sizeof(txt->text), "%s", pos);
		}

		if (strncmp(buf, "icon=", 5) == 0) {
			struct osu_icon *icon;
			if (last->icon_count == MAX_OSU_VALS)
				continue;
			icon = &last->icon[last->icon_count++];
			icon->id = atoi(buf + 5);
			pos = strchr(buf, ':');
			if (pos == NULL)
				continue;
			pos = strchr(pos + 1, ':');
			if (pos == NULL)
				continue;
			pos = strchr(pos + 1, ':');
			if (pos == NULL)
				continue;
			pos++;
			end = strchr(pos, ':');
			if (!end)
				continue;
			*end = '\0';
			snprintf(icon->lang, sizeof(icon->lang), "%s", pos);
			pos = end + 1;

			end = strchr(pos, ':');
			if (end)
				*end = '\0';
			snprintf(icon->mime_type, sizeof(icon->mime_type),
				 "%s", pos);
			if (!pos)
				continue;
			pos = end + 1;

			end = strchr(pos, ':');
			if (end)
				*end = '\0';
			snprintf(icon->filename, sizeof(icon->filename),
				 "%s", pos);
			continue;
		}
	}

	fclose(f);

	*count = osu_count;
	return osu;
}


static int osu_connect(struct hs20_osu_client *ctx, const char *bssid,
		       const char *ssid, const char *ssid2, const char *url,
		       unsigned int methods, int no_prod_assoc,
		       const char *osu_nai, const char *osu_nai2)
{
	int id;
	const char *ifname = ctx->ifname;
	char buf[200];
	struct wpa_ctrl *mon;
	int res;

	if (ssid2 && ssid2[0] == '\0')
		ssid2 = NULL;

	if (ctx->osu_ssid) {
		if (os_strcmp(ssid, ctx->osu_ssid) == 0) {
			wpa_printf(MSG_DEBUG,
				   "Enforced OSU SSID matches ANQP info");
			ssid2 = NULL;
		} else if (ssid2 && os_strcmp(ssid2, ctx->osu_ssid) == 0) {
			wpa_printf(MSG_DEBUG,
				   "Enforced OSU SSID matches RSN[OSEN] info");
			ssid = ssid2;
		} else {
			wpa_printf(MSG_INFO, "Enforced OSU SSID did not match");
			write_summary(ctx, "Enforced OSU SSID did not match");
			return -1;
		}
	}

	id = add_network(ifname);
	if (id < 0)
		return -1;
	if (set_network_quoted(ifname, id, "ssid", ssid) < 0)
		return -1;
	if (ssid2)
		osu_nai = osu_nai2;
	if (osu_nai && os_strlen(osu_nai) > 0) {
		char dir[255], fname[300];
		if (getcwd(dir, sizeof(dir)) == NULL)
			return -1;
		os_snprintf(fname, sizeof(fname), "%s/osu-ca.pem", dir);

		if (ssid2 && set_network_quoted(ifname, id, "ssid", ssid2) < 0)
			return -1;

		if (set_network(ifname, id, "proto", "OSEN") < 0 ||
		    set_network(ifname, id, "key_mgmt", "OSEN") < 0 ||
		    set_network(ifname, id, "pairwise", "CCMP") < 0 ||
		    set_network(ifname, id, "group", "GTK_NOT_USED CCMP") < 0 ||
		    set_network(ifname, id, "eap", "WFA-UNAUTH-TLS") < 0 ||
		    set_network(ifname, id, "ocsp", "2") < 0 ||
		    set_network_quoted(ifname, id, "identity", osu_nai) < 0 ||
		    set_network_quoted(ifname, id, "ca_cert", fname) < 0)
			return -1;
	} else if (ssid2) {
		wpa_printf(MSG_INFO, "No OSU_NAI set for RSN[OSEN]");
		write_summary(ctx, "No OSU_NAI set for RSN[OSEN]");
		return -1;
	} else {
		if (set_network(ifname, id, "key_mgmt", "NONE") < 0)
			return -1;
	}

	mon = open_wpa_mon(ifname);
	if (mon == NULL)
		return -1;

	wpa_printf(MSG_INFO, "Associate with OSU SSID");
	write_summary(ctx, "Associate with OSU SSID");
	snprintf(buf, sizeof(buf), "SELECT_NETWORK %d", id);
	if (wpa_command(ifname, buf) < 0)
		return -1;

	res = get_wpa_cli_event(mon, "CTRL-EVENT-CONNECTED",
				buf, sizeof(buf));

	wpa_ctrl_detach(mon);
	wpa_ctrl_close(mon);

	if (res < 0) {
		wpa_printf(MSG_INFO, "Could not connect");
		write_summary(ctx, "Could not connect to OSU network");
		wpa_printf(MSG_INFO, "Remove OSU network connection");
		snprintf(buf, sizeof(buf), "REMOVE_NETWORK %d", id);
		wpa_command(ifname, buf);
		return -1;
	}

	write_summary(ctx, "Waiting for IP address for subscription registration");
	if (wait_ip_addr(ifname, 15) < 0) {
		wpa_printf(MSG_INFO, "Could not get IP address for WLAN - try connection anyway");
	}

	if (no_prod_assoc) {
		if (res < 0)
			return -1;
		wpa_printf(MSG_INFO, "No production connection used for testing purposes");
		write_summary(ctx, "No production connection used for testing purposes");
		return 0;
	}

	ctx->no_reconnect = 1;
	if (methods & 0x02) {
		wpa_printf(MSG_DEBUG, "Calling cmd_prov from osu_connect");
		res = cmd_prov(ctx, url);
	} else if (methods & 0x01) {
		wpa_printf(MSG_DEBUG,
			   "Calling cmd_oma_dm_prov from osu_connect");
		res = cmd_oma_dm_prov(ctx, url);
	}

	wpa_printf(MSG_INFO, "Remove OSU network connection");
	write_summary(ctx, "Remove OSU network connection");
	snprintf(buf, sizeof(buf), "REMOVE_NETWORK %d", id);
	wpa_command(ifname, buf);

	if (res < 0)
		return -1;

	wpa_printf(MSG_INFO, "Requesting reconnection with updated configuration");
	write_summary(ctx, "Requesting reconnection with updated configuration");
	if (wpa_command(ctx->ifname, "INTERWORKING_SELECT auto") < 0) {
		wpa_printf(MSG_INFO, "Failed to request wpa_supplicant to reconnect");
		write_summary(ctx, "Failed to request wpa_supplicant to reconnect");
		return -1;
	}

	return 0;
}


static int cmd_osu_select(struct hs20_osu_client *ctx, const char *dir,
			  int connect, int no_prod_assoc,
			  const char *friendly_name)
{
	char fname[255];
	FILE *f;
	struct osu_data *osu = NULL, *last = NULL;
	size_t osu_count = 0, i, j;
	int ret;

	write_summary(ctx, "OSU provider selection");

	if (dir == NULL) {
		wpa_printf(MSG_INFO, "Missing dir parameter to osu_select");
		return -1;
	}

	snprintf(fname, sizeof(fname), "%s/osu-providers.txt", dir);
	osu = parse_osu_providers(fname, &osu_count);
	if (osu == NULL) {
		wpa_printf(MSG_INFO, "Could not find any OSU providers from %s",
			   fname);
		write_result(ctx, "No OSU providers available");
		return -1;
	}

	if (friendly_name) {
		for (i = 0; i < osu_count; i++) {
			last = &osu[i];
			for (j = 0; j < last->friendly_name_count; j++) {
				if (os_strcmp(last->friendly_name[j].text,
					      friendly_name) == 0)
					break;
			}
			if (j < last->friendly_name_count)
				break;
		}
		if (i == osu_count) {
			wpa_printf(MSG_INFO, "Requested operator friendly name '%s' not found in the list of available providers",
				   friendly_name);
			write_summary(ctx, "Requested operator friendly name '%s' not found in the list of available providers",
				      friendly_name);
			free(osu);
			return -1;
		}

		wpa_printf(MSG_INFO, "OSU Provider selected based on requested operator friendly name '%s'",
			   friendly_name);
		write_summary(ctx, "OSU Provider selected based on requested operator friendly name '%s'",
			      friendly_name);
		ret = i + 1;
		goto selected;
	}

	snprintf(fname, sizeof(fname), "%s/osu-providers.html", dir);
	f = fopen(fname, "w");
	if (f == NULL) {
		wpa_printf(MSG_INFO, "Could not open %s", fname);
		free(osu);
		return -1;
	}

	fprintf(f, "<html><head>"
		"<meta http-equiv=\"Content-type\" content=\"text/html; "
		"charset=utf-8\"<title>Select service operator</title>"
		"</head><body><h1>Select service operator</h1>\n");

	if (osu_count == 0)
		fprintf(f, "No online signup available\n");

	for (i = 0; i < osu_count; i++) {
		last = &osu[i];
#ifdef ANDROID
		fprintf(f, "<p>\n"
			"<a href=\"http://localhost:12345/osu/%d\">"
			"<table><tr><td>", (int) i + 1);
#else /* ANDROID */
		fprintf(f, "<p>\n"
			"<a href=\"osu://%d\">"
			"<table><tr><td>", (int) i + 1);
#endif /* ANDROID */
		for (j = 0; j < last->icon_count; j++) {
			fprintf(f, "<img src=\"osu-icon-%d.%s\">\n",
				last->icon[j].id,
				strcasecmp(last->icon[j].mime_type,
					   "image/png") == 0 ? "png" : "icon");
		}
		fprintf(f, "<td>");
		for (j = 0; j < last->friendly_name_count; j++) {
			fprintf(f, "<small>[%s]</small> %s<br>\n",
				last->friendly_name[j].lang,
				last->friendly_name[j].text);
		}
		fprintf(f, "<tr><td colspan=2>");
		for (j = 0; j < last->serv_desc_count; j++) {
			fprintf(f, "<small>[%s]</small> %s<br>\n",
				last->serv_desc[j].lang,
				last->serv_desc[j].text);
		}
		fprintf(f, "</table></a><br><small>BSSID: %s<br>\n"
			"SSID: %s<br>\n",
			last->bssid, last->osu_ssid);
		if (last->osu_ssid2[0])
			fprintf(f, "SSID2: %s<br>\n", last->osu_ssid2);
		if (last->osu_nai[0])
			fprintf(f, "NAI: %s<br>\n", last->osu_nai);
		if (last->osu_nai2[0])
			fprintf(f, "NAI2: %s<br>\n", last->osu_nai2);
		fprintf(f, "URL: %s<br>\n"
			"methods:%s%s<br>\n"
			"</small></p>\n",
			last->url,
			last->methods & 0x01 ? " OMA-DM" : "",
			last->methods & 0x02 ? " SOAP-XML-SPP" : "");
	}

	fprintf(f, "</body></html>\n");

	fclose(f);

	snprintf(fname, sizeof(fname), "file://%s/osu-providers.html", dir);
	write_summary(ctx, "Start web browser with OSU provider selection page");
	ret = hs20_web_browser(fname);

selected:
	if (ret > 0 && (size_t) ret <= osu_count) {
		char *data;
		size_t data_len;

		wpa_printf(MSG_INFO, "Selected OSU id=%d", ret);
		last = &osu[ret - 1];
		ret = 0;
		wpa_printf(MSG_INFO, "BSSID: %s", last->bssid);
		wpa_printf(MSG_INFO, "SSID: %s", last->osu_ssid);
		if (last->osu_ssid2[0])
			wpa_printf(MSG_INFO, "SSID2: %s", last->osu_ssid2);
		wpa_printf(MSG_INFO, "URL: %s", last->url);
		write_summary(ctx, "Selected OSU provider id=%d BSSID=%s SSID=%s URL=%s",
			      ret, last->bssid, last->osu_ssid, last->url);

		ctx->friendly_name_count = last->friendly_name_count;
		for (j = 0; j < last->friendly_name_count; j++) {
			wpa_printf(MSG_INFO, "FRIENDLY_NAME: [%s]%s",
				   last->friendly_name[j].lang,
				   last->friendly_name[j].text);
			os_strlcpy(ctx->friendly_name[j].lang,
				   last->friendly_name[j].lang,
				   sizeof(ctx->friendly_name[j].lang));
			os_strlcpy(ctx->friendly_name[j].text,
				   last->friendly_name[j].text,
				   sizeof(ctx->friendly_name[j].text));
		}

		ctx->icon_count = last->icon_count;
		for (j = 0; j < last->icon_count; j++) {
			char fname[256];

			os_snprintf(fname, sizeof(fname), "%s/osu-icon-%d.%s",
				    dir, last->icon[j].id,
				    strcasecmp(last->icon[j].mime_type,
					       "image/png") == 0 ?
				    "png" : "icon");
			wpa_printf(MSG_INFO, "ICON: %s (%s)",
				   fname, last->icon[j].filename);
			os_strlcpy(ctx->icon_filename[j],
				   last->icon[j].filename,
				   sizeof(ctx->icon_filename[j]));

			data = os_readfile(fname, &data_len);
			if (data) {
				sha256_vector(1, (const u8 **) &data, &data_len,
					      ctx->icon_hash[j]);
				os_free(data);
			}
		}

		if (connect == 2) {
			if (last->methods & 0x02) {
				wpa_printf(MSG_DEBUG,
					   "Calling cmd_prov from cmd_osu_select");
				ret = cmd_prov(ctx, last->url);
			} else if (last->methods & 0x01) {
				wpa_printf(MSG_DEBUG,
					   "Calling cmd_oma_dm_prov from cmd_osu_select");
				ret = cmd_oma_dm_prov(ctx, last->url);
			} else {
				wpa_printf(MSG_DEBUG,
					   "No supported OSU provisioning method");
				ret = -1;
			}
		} else if (connect) {
			ret = osu_connect(ctx, last->bssid, last->osu_ssid,
					  last->osu_ssid2,
					  last->url, last->methods,
					  no_prod_assoc, last->osu_nai,
					  last->osu_nai2);
		}
	} else
		ret = -1;

	free(osu);

	return ret;
}


static int cmd_signup(struct hs20_osu_client *ctx, int no_prod_assoc,
		      const char *friendly_name)
{
	char dir[255];
	char fname[300], buf[400];
	struct wpa_ctrl *mon;
	const char *ifname;
	int res;

	ifname = ctx->ifname;

	if (getcwd(dir, sizeof(dir)) == NULL)
		return -1;

	snprintf(fname, sizeof(fname), "%s/osu-info", dir);
	if (mkdir(fname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0 &&
	    errno != EEXIST) {
		wpa_printf(MSG_INFO, "mkdir(%s) failed: %s",
			   fname, strerror(errno));
		return -1;
	}

	android_update_permission(fname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

	snprintf(buf, sizeof(buf), "SET osu_dir %s", fname);
	if (wpa_command(ifname, buf) < 0) {
		wpa_printf(MSG_INFO, "Failed to configure osu_dir to wpa_supplicant");
		return -1;
	}

	mon = open_wpa_mon(ifname);
	if (mon == NULL)
		return -1;

	wpa_printf(MSG_INFO, "Starting OSU fetch");
	write_summary(ctx, "Starting OSU provider information fetch");
	if (wpa_command(ifname, "FETCH_OSU") < 0) {
		wpa_printf(MSG_INFO, "Could not start OSU fetch");
		wpa_ctrl_detach(mon);
		wpa_ctrl_close(mon);
		return -1;
	}
	res = get_wpa_cli_event(mon, "OSU provider fetch completed",
				buf, sizeof(buf));

	wpa_ctrl_detach(mon);
	wpa_ctrl_close(mon);

	if (res < 0) {
		wpa_printf(MSG_INFO, "OSU fetch did not complete");
		write_summary(ctx, "OSU fetch did not complete");
		return -1;
	}
	wpa_printf(MSG_INFO, "OSU provider fetch completed");

	return cmd_osu_select(ctx, fname, 1, no_prod_assoc, friendly_name);
}


static int cmd_sub_rem(struct hs20_osu_client *ctx, const char *address,
		       const char *pps_fname, const char *ca_fname)
{
	xml_node_t *pps, *node;
	char pps_fname_buf[300];
	char ca_fname_buf[200];
	char *cred_username = NULL;
	char *cred_password = NULL;
	char *sub_rem_uri = NULL;
	char client_cert_buf[200];
	char *client_cert = NULL;
	char client_key_buf[200];
	char *client_key = NULL;
	int spp;

	wpa_printf(MSG_INFO, "Subscription remediation requested with Server URL: %s",
		   address);

	if (!pps_fname) {
		char buf[256];
		wpa_printf(MSG_INFO, "Determining PPS file based on Home SP information");
		if (os_strncmp(address, "fqdn=", 5) == 0) {
			wpa_printf(MSG_INFO, "Use requested FQDN from command line");
			os_snprintf(buf, sizeof(buf), "%s", address + 5);
			address = NULL;
		} else if (get_wpa_status(ctx->ifname, "provisioning_sp", buf,
					  sizeof(buf)) < 0) {
			wpa_printf(MSG_INFO, "Could not get provisioning Home SP FQDN from wpa_supplicant");
			return -1;
		}
		os_free(ctx->fqdn);
		ctx->fqdn = os_strdup(buf);
		if (ctx->fqdn == NULL)
			return -1;
		wpa_printf(MSG_INFO, "Home SP FQDN for current credential: %s",
			   buf);
		os_snprintf(pps_fname_buf, sizeof(pps_fname_buf),
			    "SP/%s/pps.xml", ctx->fqdn);
		pps_fname = pps_fname_buf;

		os_snprintf(ca_fname_buf, sizeof(ca_fname_buf), "SP/%s/ca.pem",
			    ctx->fqdn);
		ca_fname = ca_fname_buf;
	}

	if (!os_file_exists(pps_fname)) {
		wpa_printf(MSG_INFO, "PPS file '%s' does not exist or is not accessible",
			   pps_fname);
		return -1;
	}
	wpa_printf(MSG_INFO, "Using PPS file: %s", pps_fname);

	if (ca_fname && !os_file_exists(ca_fname)) {
		wpa_printf(MSG_INFO, "CA file '%s' does not exist or is not accessible",
			   ca_fname);
		return -1;
	}
	wpa_printf(MSG_INFO, "Using server trust root: %s", ca_fname);
	ctx->ca_fname = ca_fname;

	pps = node_from_file(ctx->xml, pps_fname);
	if (pps == NULL) {
		wpa_printf(MSG_INFO, "Could not read PPS MO");
		return -1;
	}

	if (!ctx->fqdn) {
		char *tmp;
		node = get_child_node(ctx->xml, pps, "HomeSP/FQDN");
		if (node == NULL) {
			wpa_printf(MSG_INFO, "No HomeSP/FQDN found from PPS");
			return -1;
		}
		tmp = xml_node_get_text(ctx->xml, node);
		if (tmp == NULL) {
			wpa_printf(MSG_INFO, "No HomeSP/FQDN text found from PPS");
			return -1;
		}
		ctx->fqdn = os_strdup(tmp);
		xml_node_get_text_free(ctx->xml, tmp);
		if (!ctx->fqdn) {
			wpa_printf(MSG_INFO, "No FQDN known");
			return -1;
		}
	}

	node = get_child_node(ctx->xml, pps,
			      "SubscriptionUpdate/UpdateMethod");
	if (node) {
		char *tmp;
		tmp = xml_node_get_text(ctx->xml, node);
		if (tmp && os_strcasecmp(tmp, "OMA-DM-ClientInitiated") == 0)
			spp = 0;
		else
			spp = 1;
	} else {
		wpa_printf(MSG_INFO, "No UpdateMethod specified - assume SPP");
		spp = 1;
	}

	get_user_pw(ctx, pps, "SubscriptionUpdate/UsernamePassword",
		    &cred_username, &cred_password);
	if (cred_username)
		wpa_printf(MSG_INFO, "Using username: %s", cred_username);
	if (cred_password)
		wpa_printf(MSG_DEBUG, "Using password: %s", cred_password);

	if (cred_username == NULL && cred_password == NULL &&
	    get_child_node(ctx->xml, pps, "Credential/DigitalCertificate")) {
		wpa_printf(MSG_INFO, "Using client certificate");
		os_snprintf(client_cert_buf, sizeof(client_cert_buf),
			    "SP/%s/client-cert.pem", ctx->fqdn);
		client_cert = client_cert_buf;
		os_snprintf(client_key_buf, sizeof(client_key_buf),
			    "SP/%s/client-key.pem", ctx->fqdn);
		client_key = client_key_buf;
		ctx->client_cert_present = 1;
	}

	node = get_child_node(ctx->xml, pps, "SubscriptionUpdate/URI");
	if (node) {
		sub_rem_uri = xml_node_get_text(ctx->xml, node);
		if (sub_rem_uri &&
		    (!address || os_strcmp(address, sub_rem_uri) != 0)) {
			wpa_printf(MSG_INFO, "Override sub rem URI based on PPS: %s",
				   sub_rem_uri);
			address = sub_rem_uri;
		}
	}
	if (!address) {
		wpa_printf(MSG_INFO, "Server URL not known");
		return -1;
	}

	write_summary(ctx, "Wait for IP address for subscriptiom remediation");
	wpa_printf(MSG_INFO, "Wait for IP address before starting subscription remediation");

	if (wait_ip_addr(ctx->ifname, 15) < 0) {
		wpa_printf(MSG_INFO, "Could not get IP address for WLAN - try connection anyway");
	}

	if (spp)
		spp_sub_rem(ctx, address, pps_fname,
			    client_cert, client_key,
			    cred_username, cred_password, pps);
	else
		oma_dm_sub_rem(ctx, address, pps_fname,
			       client_cert, client_key,
			       cred_username, cred_password, pps);

	xml_node_get_text_free(ctx->xml, sub_rem_uri);
	xml_node_get_text_free(ctx->xml, cred_username);
	str_clear_free(cred_password);
	xml_node_free(ctx->xml, pps);
	return 0;
}


static int cmd_pol_upd(struct hs20_osu_client *ctx, const char *address,
		       const char *pps_fname, const char *ca_fname)
{
	xml_node_t *pps;
	xml_node_t *node;
	char pps_fname_buf[300];
	char ca_fname_buf[200];
	char *uri = NULL;
	char *cred_username = NULL;
	char *cred_password = NULL;
	char client_cert_buf[200];
	char *client_cert = NULL;
	char client_key_buf[200];
	char *client_key = NULL;
	int spp;

	wpa_printf(MSG_INFO, "Policy update requested");

	if (!pps_fname) {
		char buf[256];
		wpa_printf(MSG_INFO, "Determining PPS file based on Home SP information");
		if (address && os_strncmp(address, "fqdn=", 5) == 0) {
			wpa_printf(MSG_INFO, "Use requested FQDN from command line");
			os_snprintf(buf, sizeof(buf), "%s", address + 5);
			address = NULL;
		} else if (get_wpa_status(ctx->ifname, "provisioning_sp", buf,
					  sizeof(buf)) < 0) {
			wpa_printf(MSG_INFO, "Could not get provisioning Home SP FQDN from wpa_supplicant");
			return -1;
		}
		os_free(ctx->fqdn);
		ctx->fqdn = os_strdup(buf);
		if (ctx->fqdn == NULL)
			return -1;
		wpa_printf(MSG_INFO, "Home SP FQDN for current credential: %s",
			   buf);
		os_snprintf(pps_fname_buf, sizeof(pps_fname_buf),
			    "SP/%s/pps.xml", ctx->fqdn);
		pps_fname = pps_fname_buf;

		os_snprintf(ca_fname_buf, sizeof(ca_fname_buf), "SP/%s/ca.pem",
			    buf);
		ca_fname = ca_fname_buf;
	}

	if (!os_file_exists(pps_fname)) {
		wpa_printf(MSG_INFO, "PPS file '%s' does not exist or is not accessible",
			   pps_fname);
		return -1;
	}
	wpa_printf(MSG_INFO, "Using PPS file: %s", pps_fname);

	if (ca_fname && !os_file_exists(ca_fname)) {
		wpa_printf(MSG_INFO, "CA file '%s' does not exist or is not accessible",
			   ca_fname);
		return -1;
	}
	wpa_printf(MSG_INFO, "Using server trust root: %s", ca_fname);
	ctx->ca_fname = ca_fname;

	pps = node_from_file(ctx->xml, pps_fname);
	if (pps == NULL) {
		wpa_printf(MSG_INFO, "Could not read PPS MO");
		return -1;
	}

	if (!ctx->fqdn) {
		char *tmp;
		node = get_child_node(ctx->xml, pps, "HomeSP/FQDN");
		if (node == NULL) {
			wpa_printf(MSG_INFO, "No HomeSP/FQDN found from PPS");
			return -1;
		}
		tmp = xml_node_get_text(ctx->xml, node);
		if (tmp == NULL) {
			wpa_printf(MSG_INFO, "No HomeSP/FQDN text found from PPS");
			return -1;
		}
		ctx->fqdn = os_strdup(tmp);
		xml_node_get_text_free(ctx->xml, tmp);
		if (!ctx->fqdn) {
			wpa_printf(MSG_INFO, "No FQDN known");
			return -1;
		}
	}

	node = get_child_node(ctx->xml, pps,
			      "Policy/PolicyUpdate/UpdateMethod");
	if (node) {
		char *tmp;
		tmp = xml_node_get_text(ctx->xml, node);
		if (tmp && os_strcasecmp(tmp, "OMA-DM-ClientInitiated") == 0)
			spp = 0;
		else
			spp = 1;
	} else {
		wpa_printf(MSG_INFO, "No UpdateMethod specified - assume SPP");
		spp = 1;
	}

	get_user_pw(ctx, pps, "Policy/PolicyUpdate/UsernamePassword",
		    &cred_username, &cred_password);
	if (cred_username)
		wpa_printf(MSG_INFO, "Using username: %s", cred_username);
	if (cred_password)
		wpa_printf(MSG_DEBUG, "Using password: %s", cred_password);

	if (cred_username == NULL && cred_password == NULL &&
	    get_child_node(ctx->xml, pps, "Credential/DigitalCertificate")) {
		wpa_printf(MSG_INFO, "Using client certificate");
		os_snprintf(client_cert_buf, sizeof(client_cert_buf),
			    "SP/%s/client-cert.pem", ctx->fqdn);
		client_cert = client_cert_buf;
		os_snprintf(client_key_buf, sizeof(client_key_buf),
			    "SP/%s/client-key.pem", ctx->fqdn);
		client_key = client_key_buf;
	}

	if (!address) {
		node = get_child_node(ctx->xml, pps, "Policy/PolicyUpdate/URI");
		if (node) {
			uri = xml_node_get_text(ctx->xml, node);
			wpa_printf(MSG_INFO, "URI based on PPS: %s", uri);
			address = uri;
		}
	}
	if (!address) {
		wpa_printf(MSG_INFO, "Server URL not known");
		return -1;
	}

	if (spp)
		spp_pol_upd(ctx, address, pps_fname,
			    client_cert, client_key,
			    cred_username, cred_password, pps);
	else
		oma_dm_pol_upd(ctx, address, pps_fname,
			       client_cert, client_key,
			       cred_username, cred_password, pps);

	xml_node_get_text_free(ctx->xml, uri);
	xml_node_get_text_free(ctx->xml, cred_username);
	str_clear_free(cred_password);
	xml_node_free(ctx->xml, pps);

	return 0;
}


static char * get_hostname(const char *url)
{
	const char *pos, *end, *end2;
	char *ret;

	if (url == NULL)
		return NULL;

	pos = os_strchr(url, '/');
	if (pos == NULL)
		return NULL;
	pos++;
	if (*pos != '/')
		return NULL;
	pos++;

	end = os_strchr(pos, '/');
	end2 = os_strchr(pos, ':');
	if ((end && end2 && end2 < end) || (!end && end2))
		end = end2;
	if (end)
		end--;
	else {
		end = pos;
		while (*end)
			end++;
		if (end > pos)
			end--;
	}

	ret = os_malloc(end - pos + 2);
	if (ret == NULL)
		return NULL;

	os_memcpy(ret, pos, end - pos + 1);
	ret[end - pos + 1] = '\0';

	return ret;
}


static int osu_cert_cb(void *_ctx, struct http_cert *cert)
{
	struct hs20_osu_client *ctx = _ctx;
	unsigned int i, j;
	int found;
	char *host = NULL;

	wpa_printf(MSG_INFO, "osu_cert_cb(osu_cert_validation=%d, url=%s)",
		   !ctx->no_osu_cert_validation, ctx->server_url);

	host = get_hostname(ctx->server_url);

	for (i = 0; i < ctx->server_dnsname_count; i++)
		os_free(ctx->server_dnsname[i]);
	os_free(ctx->server_dnsname);
	ctx->server_dnsname = os_calloc(cert->num_dnsname, sizeof(char *));
	ctx->server_dnsname_count = 0;

	found = 0;
	for (i = 0; i < cert->num_dnsname; i++) {
		if (ctx->server_dnsname) {
			ctx->server_dnsname[ctx->server_dnsname_count] =
				os_strdup(cert->dnsname[i]);
			if (ctx->server_dnsname[ctx->server_dnsname_count])
				ctx->server_dnsname_count++;
		}
		if (host && os_strcasecmp(host, cert->dnsname[i]) == 0)
			found = 1;
		wpa_printf(MSG_INFO, "dNSName '%s'", cert->dnsname[i]);
	}

	if (host && !found) {
		wpa_printf(MSG_INFO, "Server name from URL (%s) did not match any dNSName - abort connection",
			   host);
		write_result(ctx, "Server name from URL (%s) did not match any dNSName - abort connection",
			     host);
		os_free(host);
		return -1;
	}

	os_free(host);

	for (i = 0; i < cert->num_othername; i++) {
		if (os_strcmp(cert->othername[i].oid,
			      "1.3.6.1.4.1.40808.1.1.1") == 0) {
			wpa_hexdump_ascii(MSG_INFO,
					  "id-wfa-hotspot-friendlyName",
					  cert->othername[i].data,
					  cert->othername[i].len);
		}
	}

	for (j = 0; !ctx->no_osu_cert_validation &&
		     j < ctx->friendly_name_count; j++) {
		int found = 0;
		for (i = 0; i < cert->num_othername; i++) {
			if (os_strcmp(cert->othername[i].oid,
				      "1.3.6.1.4.1.40808.1.1.1") != 0)
				continue;
			if (cert->othername[i].len < 3)
				continue;
			if (os_strncasecmp((char *) cert->othername[i].data,
					   ctx->friendly_name[j].lang, 3) != 0)
				continue;
			if (os_strncmp((char *) cert->othername[i].data + 3,
				       ctx->friendly_name[j].text,
				       cert->othername[i].len - 3) == 0) {
				found = 1;
				break;
			}
		}

		if (!found) {
			wpa_printf(MSG_INFO, "No friendly name match found for '[%s]%s'",
				   ctx->friendly_name[j].lang,
				   ctx->friendly_name[j].text);
			write_result(ctx, "No friendly name match found for '[%s]%s'",
				     ctx->friendly_name[j].lang,
				     ctx->friendly_name[j].text);
			return -1;
		}
	}

	for (i = 0; i < cert->num_logo; i++) {
		struct http_logo *logo = &cert->logo[i];

		wpa_printf(MSG_INFO, "logo hash alg %s uri '%s'",
			   logo->alg_oid, logo->uri);
		wpa_hexdump_ascii(MSG_INFO, "hashValue",
				  logo->hash, logo->hash_len);
	}

	for (j = 0; !ctx->no_osu_cert_validation && j < ctx->icon_count; j++) {
		int found = 0;
		char *name = ctx->icon_filename[j];
		size_t name_len = os_strlen(name);

		wpa_printf(MSG_INFO,
			   "[%i] Looking for icon file name '%s' match",
			   j, name);
		for (i = 0; i < cert->num_logo; i++) {
			struct http_logo *logo = &cert->logo[i];
			size_t uri_len = os_strlen(logo->uri);
			char *pos;

			wpa_printf(MSG_INFO,
				   "[%i] Comparing to '%s' uri_len=%d name_len=%d",
				   i, logo->uri, (int) uri_len, (int) name_len);
			if (uri_len < 1 + name_len) {
				wpa_printf(MSG_INFO, "URI Length is too short");
				continue;
			}
			pos = &logo->uri[uri_len - name_len - 1];
			if (*pos != '/')
				continue;
			pos++;
			if (os_strcmp(pos, name) == 0) {
				found = 1;
				break;
			}
		}

		if (!found) {
			wpa_printf(MSG_INFO, "No icon filename match found for '%s'",
				   name);
			write_result(ctx,
				     "No icon filename match found for '%s'",
				     name);
			return -1;
		}
	}

	for (j = 0; !ctx->no_osu_cert_validation && j < ctx->icon_count; j++) {
		int found = 0;

		for (i = 0; i < cert->num_logo; i++) {
			struct http_logo *logo = &cert->logo[i];

			if (logo->hash_len != 32) {
				wpa_printf(MSG_INFO,
					   "[%i][%i] Icon hash length invalid (should be 32): %d",
					   j, i, (int) logo->hash_len);
				continue;
			}
			if (os_memcmp(logo->hash, ctx->icon_hash[j], 32) == 0) {
				found = 1;
				break;
			}

			wpa_printf(MSG_DEBUG,
				   "[%u][%u] Icon hash did not match", j, i);
			wpa_hexdump_ascii(MSG_DEBUG, "logo->hash",
					  logo->hash, 32);
			wpa_hexdump_ascii(MSG_DEBUG, "ctx->icon_hash[j]",
					  ctx->icon_hash[j], 32);
		}

		if (!found) {
			wpa_printf(MSG_INFO,
				   "No icon hash match (by hash) found");
			write_result(ctx,
				     "No icon hash match (by hash) found");
			return -1;
		}
	}

	return 0;
}


static int init_ctx(struct hs20_osu_client *ctx)
{
	xml_node_t *devinfo, *devid;

	os_memset(ctx, 0, sizeof(*ctx));
	ctx->ifname = "wlan0";
	ctx->xml = xml_node_init_ctx(ctx, NULL);
	if (ctx->xml == NULL)
		return -1;

	devinfo = node_from_file(ctx->xml, "devinfo.xml");
	if (devinfo) {
		devid = get_node(ctx->xml, devinfo, "DevId");
		if (devid) {
			char *tmp = xml_node_get_text(ctx->xml, devid);

			if (tmp) {
				ctx->devid = os_strdup(tmp);
				xml_node_get_text_free(ctx->xml, tmp);
			}
		}
		xml_node_free(ctx->xml, devinfo);
	}

	ctx->http = http_init_ctx(ctx, ctx->xml);
	if (ctx->http == NULL) {
		xml_node_deinit_ctx(ctx->xml);
		return -1;
	}
	http_ocsp_set(ctx->http, 2);
	http_set_cert_cb(ctx->http, osu_cert_cb, ctx);

	return 0;
}


static void deinit_ctx(struct hs20_osu_client *ctx)
{
	size_t i;

	http_deinit_ctx(ctx->http);
	xml_node_deinit_ctx(ctx->xml);
	os_free(ctx->fqdn);
	os_free(ctx->server_url);
	os_free(ctx->devid);

	for (i = 0; i < ctx->server_dnsname_count; i++)
		os_free(ctx->server_dnsname[i]);
	os_free(ctx->server_dnsname);
}


static void check_workarounds(struct hs20_osu_client *ctx)
{
	FILE *f;
	char buf[100];
	unsigned long int val = 0;

	f = fopen("hs20-osu-client.workarounds", "r");
	if (f == NULL)
		return;

	if (fgets(buf, sizeof(buf), f))
		val = strtoul(buf, NULL, 16);

	fclose(f);

	if (val) {
		wpa_printf(MSG_INFO, "Workarounds enabled: 0x%lx", val);
		ctx->workarounds = val;
		if (ctx->workarounds & WORKAROUND_OCSP_OPTIONAL)
			http_ocsp_set(ctx->http, 1);
	}
}


static void usage(void)
{
	printf("usage: hs20-osu-client [-dddqqKt] [-S<station ifname>] \\\n"
	       "    [-w<wpa_supplicant ctrl_iface dir>] "
	       "[-r<result file>] [-f<debug file>] \\\n"
	       "    [-s<summary file>] \\\n"
	       "    [-x<spp.xsd file name>] \\\n"
	       "    <command> [arguments..]\n"
	       "commands:\n"
	       "- to_tnds <XML MO> <XML MO in TNDS format> [URN]\n"
	       "- to_tnds2 <XML MO> <XML MO in TNDS format (Path) "
	       "[URN]>\n"
	       "- from_tnds <XML MO in TNDS format> <XML MO>\n"
	       "- set_pps <PerProviderSubscription XML file name>\n"
	       "- get_fqdn <PerProviderSubscription XML file name>\n"
	       "- pol_upd [Server URL] [PPS] [CA cert]\n"
	       "- sub_rem <Server URL> [PPS] [CA cert]\n"
	       "- prov <Server URL> [CA cert]\n"
	       "- oma_dm_prov <Server URL> [CA cert]\n"
	       "- sim_prov <Server URL> [CA cert]\n"
	       "- oma_dm_sim_prov <Server URL> [CA cert]\n"
	       "- signup [CA cert]\n"
	       "- dl_osu_ca <PPS> <CA file>\n"
	       "- dl_polupd_ca <PPS> <CA file>\n"
	       "- dl_aaa_ca <PPS> <CA file>\n"
	       "- browser <URL>\n"
	       "- parse_cert <X.509 certificate (DER)>\n"
	       "- osu_select <OSU info directory> [CA cert]\n");
}


int main(int argc, char *argv[])
{
	struct hs20_osu_client ctx;
	int c;
	int ret = 0;
	int no_prod_assoc = 0;
	const char *friendly_name = NULL;
	const char *wpa_debug_file_path = NULL;
	extern char *wpas_ctrl_path;
	extern int wpa_debug_level;
	extern int wpa_debug_show_keys;
	extern int wpa_debug_timestamp;

	if (init_ctx(&ctx) < 0)
		return -1;

	for (;;) {
		c = getopt(argc, argv, "df:hKNo:O:qr:s:S:tw:x:");
		if (c < 0)
			break;
		switch (c) {
		case 'd':
			if (wpa_debug_level > 0)
				wpa_debug_level--;
			break;
		case 'f':
			wpa_debug_file_path = optarg;
			break;
		case 'K':
			wpa_debug_show_keys++;
			break;
		case 'N':
			no_prod_assoc = 1;
			break;
		case 'o':
			ctx.osu_ssid = optarg;
			break;
		case 'O':
			friendly_name = optarg;
			break;
		case 'q':
			wpa_debug_level++;
			break;
		case 'r':
			ctx.result_file = optarg;
			break;
		case 's':
			ctx.summary_file = optarg;
			break;
		case 'S':
			ctx.ifname = optarg;
			break;
		case 't':
			wpa_debug_timestamp++;
			break;
		case 'w':
			wpas_ctrl_path = optarg;
			break;
		case 'x':
			spp_xsd_fname = optarg;
			break;
		case 'h':
		default:
			usage();
			exit(0);
			break;
		}
	}

	if (argc - optind < 1) {
		usage();
		exit(0);
	}

	wpa_debug_open_file(wpa_debug_file_path);

#ifdef __linux__
	setlinebuf(stdout);
#endif /* __linux__ */

	if (ctx.result_file)
		unlink(ctx.result_file);
	wpa_printf(MSG_DEBUG, "===[hs20-osu-client START - command: %s ]======"
		   "================", argv[optind]);
	check_workarounds(&ctx);

	if (strcmp(argv[optind], "to_tnds") == 0) {
		if (argc - optind < 2) {
			usage();
			exit(0);
		}
		cmd_to_tnds(&ctx, argv[optind + 1], argv[optind + 2],
			    argc > optind + 3 ? argv[optind + 3] : NULL,
			    0);
	} else if (strcmp(argv[optind], "to_tnds2") == 0) {
		if (argc - optind < 2) {
			usage();
			exit(0);
		}
		cmd_to_tnds(&ctx, argv[optind + 1], argv[optind + 2],
			    argc > optind + 3 ? argv[optind + 3] : NULL,
			    1);
	} else if (strcmp(argv[optind], "from_tnds") == 0) {
		if (argc - optind < 2) {
			usage();
			exit(0);
		}
		cmd_from_tnds(&ctx, argv[optind + 1], argv[optind + 2]);
	} else if (strcmp(argv[optind], "sub_rem") == 0) {
		if (argc - optind < 2) {
			usage();
			exit(0);
		}
		ret = cmd_sub_rem(&ctx, argv[optind + 1],
				  argc > optind + 2 ? argv[optind + 2] : NULL,
				  argc > optind + 3 ? argv[optind + 3] : NULL);
	} else if (strcmp(argv[optind], "pol_upd") == 0) {
		ret = cmd_pol_upd(&ctx,
				  argc > optind + 1 ? argv[optind + 1] : NULL,
				  argc > optind + 2 ? argv[optind + 2] : NULL,
				  argc > optind + 3 ? argv[optind + 3] : NULL);
	} else if (strcmp(argv[optind], "prov") == 0) {
		if (argc - optind < 2) {
			usage();
			exit(0);
		}
		ctx.ca_fname = argv[optind + 2];
		wpa_printf(MSG_DEBUG, "Calling cmd_prov from main");
		cmd_prov(&ctx, argv[optind + 1]);
	} else if (strcmp(argv[optind], "sim_prov") == 0) {
		if (argc - optind < 2) {
			usage();
			exit(0);
		}
		ctx.ca_fname = argv[optind + 2];
		cmd_sim_prov(&ctx, argv[optind + 1]);
	} else if (strcmp(argv[optind], "dl_osu_ca") == 0) {
		if (argc - optind < 2) {
			usage();
			exit(0);
		}
		cmd_dl_osu_ca(&ctx, argv[optind + 1], argv[optind + 2]);
	} else if (strcmp(argv[optind], "dl_polupd_ca") == 0) {
		if (argc - optind < 2) {
			usage();
			exit(0);
		}
		cmd_dl_polupd_ca(&ctx, argv[optind + 1], argv[optind + 2]);
	} else if (strcmp(argv[optind], "dl_aaa_ca") == 0) {
		if (argc - optind < 2) {
			usage();
			exit(0);
		}
		cmd_dl_aaa_ca(&ctx, argv[optind + 1], argv[optind + 2]);
	} else if (strcmp(argv[optind], "osu_select") == 0) {
		if (argc - optind < 2) {
			usage();
			exit(0);
		}
		ctx.ca_fname = argc > optind + 2 ? argv[optind + 2] : NULL;
		cmd_osu_select(&ctx, argv[optind + 1], 2, 1, NULL);
	} else if (strcmp(argv[optind], "signup") == 0) {
		ctx.ca_fname = argc > optind + 1 ? argv[optind + 1] : NULL;
		ret = cmd_signup(&ctx, no_prod_assoc, friendly_name);
	} else if (strcmp(argv[optind], "set_pps") == 0) {
		if (argc - optind < 2) {
			usage();
			exit(0);
		}
		cmd_set_pps(&ctx, argv[optind + 1]);
	} else if (strcmp(argv[optind], "get_fqdn") == 0) {
		if (argc - optind < 1) {
			usage();
			exit(0);
		}
		ret = cmd_get_fqdn(&ctx, argv[optind + 1]);
	} else if (strcmp(argv[optind], "oma_dm_prov") == 0) {
		if (argc - optind < 2) {
			usage();
			exit(0);
		}
		ctx.ca_fname = argv[optind + 2];
		cmd_oma_dm_prov(&ctx, argv[optind + 1]);
	} else if (strcmp(argv[optind], "oma_dm_sim_prov") == 0) {
		if (argc - optind < 2) {
			usage();
			exit(0);
		}
		ctx.ca_fname = argv[optind + 2];
		if (cmd_oma_dm_sim_prov(&ctx, argv[optind + 1]) < 0) {
			write_summary(&ctx, "Failed to complete OMA DM SIM provisioning");
			return -1;
		}
	} else if (strcmp(argv[optind], "oma_dm_add") == 0) {
		if (argc - optind < 2) {
			usage();
			exit(0);
		}
		cmd_oma_dm_add(&ctx, argv[optind + 1], argv[optind + 2]);
	} else if (strcmp(argv[optind], "oma_dm_replace") == 0) {
		if (argc - optind < 2) {
			usage();
			exit(0);
		}
		cmd_oma_dm_replace(&ctx, argv[optind + 1], argv[optind + 2]);
	} else if (strcmp(argv[optind], "est_csr") == 0) {
		if (argc - optind < 2) {
			usage();
			exit(0);
		}
		mkdir("Cert", S_IRWXU);
		est_build_csr(&ctx, argv[optind + 1]);
	} else if (strcmp(argv[optind], "browser") == 0) {
		int ret;

		if (argc - optind < 2) {
			usage();
			exit(0);
		}

		wpa_printf(MSG_INFO, "Launch web browser to URL %s",
			   argv[optind + 1]);
		ret = hs20_web_browser(argv[optind + 1]);
		wpa_printf(MSG_INFO, "Web browser result: %d", ret);
	} else if (strcmp(argv[optind], "parse_cert") == 0) {
		if (argc - optind < 2) {
			usage();
			exit(0);
		}

		wpa_debug_level = MSG_MSGDUMP;
		http_parse_x509_certificate(ctx.http, argv[optind + 1]);
		wpa_debug_level = MSG_INFO;
	} else {
		wpa_printf(MSG_INFO, "Unknown command '%s'", argv[optind]);
	}

	deinit_ctx(&ctx);
	wpa_printf(MSG_DEBUG,
		   "===[hs20-osu-client END ]======================");

	wpa_debug_close_file();

	return ret;
}
