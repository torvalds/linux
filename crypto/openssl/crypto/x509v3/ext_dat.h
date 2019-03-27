/*
 * Copyright 1999-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

int name_cmp(const char *name, const char *cmp);

extern const X509V3_EXT_METHOD v3_bcons, v3_nscert, v3_key_usage, v3_ext_ku;
extern const X509V3_EXT_METHOD v3_pkey_usage_period, v3_sxnet, v3_info, v3_sinfo;
extern const X509V3_EXT_METHOD v3_ns_ia5_list[8], v3_alt[3], v3_skey_id, v3_akey_id;
extern const X509V3_EXT_METHOD v3_crl_num, v3_crl_reason, v3_crl_invdate;
extern const X509V3_EXT_METHOD v3_delta_crl, v3_cpols, v3_crld, v3_freshest_crl;
extern const X509V3_EXT_METHOD v3_ocsp_nonce, v3_ocsp_accresp, v3_ocsp_acutoff;
extern const X509V3_EXT_METHOD v3_ocsp_crlid, v3_ocsp_nocheck, v3_ocsp_serviceloc;
extern const X509V3_EXT_METHOD v3_crl_hold, v3_pci;
extern const X509V3_EXT_METHOD v3_policy_mappings, v3_policy_constraints;
extern const X509V3_EXT_METHOD v3_name_constraints, v3_inhibit_anyp, v3_idp;
extern const X509V3_EXT_METHOD v3_addr, v3_asid;
extern const X509V3_EXT_METHOD v3_ct_scts[3];
extern const X509V3_EXT_METHOD v3_tls_feature;
extern const X509V3_EXT_METHOD v3_ext_admission;
