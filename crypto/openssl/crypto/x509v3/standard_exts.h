/*
 * Copyright 1999-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * This table will be searched using OBJ_bsearch so it *must* kept in order
 * of the ext_nid values.
 */

static const X509V3_EXT_METHOD *standard_exts[] = {
    &v3_nscert,
    &v3_ns_ia5_list[0],
    &v3_ns_ia5_list[1],
    &v3_ns_ia5_list[2],
    &v3_ns_ia5_list[3],
    &v3_ns_ia5_list[4],
    &v3_ns_ia5_list[5],
    &v3_ns_ia5_list[6],
    &v3_skey_id,
    &v3_key_usage,
    &v3_pkey_usage_period,
    &v3_alt[0],
    &v3_alt[1],
    &v3_bcons,
    &v3_crl_num,
    &v3_cpols,
    &v3_akey_id,
    &v3_crld,
    &v3_ext_ku,
    &v3_delta_crl,
    &v3_crl_reason,
#ifndef OPENSSL_NO_OCSP
    &v3_crl_invdate,
#endif
    &v3_sxnet,
    &v3_info,
#ifndef OPENSSL_NO_RFC3779
    &v3_addr,
    &v3_asid,
#endif
#ifndef OPENSSL_NO_OCSP
    &v3_ocsp_nonce,
    &v3_ocsp_crlid,
    &v3_ocsp_accresp,
    &v3_ocsp_nocheck,
    &v3_ocsp_acutoff,
    &v3_ocsp_serviceloc,
#endif
    &v3_sinfo,
    &v3_policy_constraints,
#ifndef OPENSSL_NO_OCSP
    &v3_crl_hold,
#endif
    &v3_pci,
    &v3_name_constraints,
    &v3_policy_mappings,
    &v3_inhibit_anyp,
    &v3_idp,
    &v3_alt[2],
    &v3_freshest_crl,
#ifndef OPENSSL_NO_CT
    &v3_ct_scts[0],
    &v3_ct_scts[1],
    &v3_ct_scts[2],
#endif
    &v3_tls_feature,
    &v3_ext_admission
};

/* Number of standard extensions */

#define STANDARD_EXTENSION_COUNT OSSL_NELEM(standard_exts)

