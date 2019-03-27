/*
 * Copyright (c) 1997 - 2000 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "gsskrb5_locl.h"
#include <gssapi_mech.h>

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {10, (void *)"\x2a\x86\x48\x86\xf7\x12"
 *              "\x01\x02\x01\x01"},
 * corresponding to an object-identifier value of
 * {iso(1) member-body(2) United States(840) mit(113554)
 *  infosys(1) gssapi(2) generic(1) user_name(1)}.  The constant
 * GSS_C_NT_USER_NAME should be initialized to point
 * to that gss_OID_desc.
 */

gss_OID_desc GSSAPI_LIB_VARIABLE __gss_c_nt_user_name_oid_desc =
    {10, rk_UNCONST("\x2a\x86\x48\x86\xf7\x12" "\x01\x02\x01\x01")};

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {10, (void *)"\x2a\x86\x48\x86\xf7\x12"
 *              "\x01\x02\x01\x02"},
 * corresponding to an object-identifier value of
 * {iso(1) member-body(2) United States(840) mit(113554)
 *  infosys(1) gssapi(2) generic(1) machine_uid_name(2)}.
 * The constant GSS_C_NT_MACHINE_UID_NAME should be
 * initialized to point to that gss_OID_desc.
 */

gss_OID_desc GSSAPI_LIB_VARIABLE __gss_c_nt_machine_uid_name_oid_desc =
    {10, rk_UNCONST("\x2a\x86\x48\x86\xf7\x12" "\x01\x02\x01\x02")};

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {10, (void *)"\x2a\x86\x48\x86\xf7\x12"
 *              "\x01\x02\x01\x03"},
 * corresponding to an object-identifier value of
 * {iso(1) member-body(2) United States(840) mit(113554)
 *  infosys(1) gssapi(2) generic(1) string_uid_name(3)}.
 * The constant GSS_C_NT_STRING_UID_NAME should be
 * initialized to point to that gss_OID_desc.
 */

gss_OID_desc GSSAPI_LIB_VARIABLE __gss_c_nt_string_uid_name_oid_desc =
    {10, rk_UNCONST("\x2a\x86\x48\x86\xf7\x12" "\x01\x02\x01\x03")};

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {6, (void *)"\x2b\x06\x01\x05\x06\x02"},
 * corresponding to an object-identifier value of
 * {iso(1) org(3) dod(6) internet(1) security(5)
 * nametypes(6) gss-host-based-services(2)).  The constant
 * GSS_C_NT_HOSTBASED_SERVICE_X should be initialized to point
 * to that gss_OID_desc.  This is a deprecated OID value, and
 * implementations wishing to support hostbased-service names
 * should instead use the GSS_C_NT_HOSTBASED_SERVICE OID,
 * defined below, to identify such names;
 * GSS_C_NT_HOSTBASED_SERVICE_X should be accepted a synonym
 * for GSS_C_NT_HOSTBASED_SERVICE when presented as an input
 * parameter, but should not be emitted by GSS-API
 * implementations
 */

gss_OID_desc GSSAPI_LIB_VARIABLE __gss_c_nt_hostbased_service_x_oid_desc =
    {6, rk_UNCONST("\x2b\x06\x01\x05\x06\x02")};

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {10, (void *)"\x2a\x86\x48\x86\xf7\x12"
 *              "\x01\x02\x01\x04"}, corresponding to an
 * object-identifier value of {iso(1) member-body(2)
 * Unites States(840) mit(113554) infosys(1) gssapi(2)
 * generic(1) service_name(4)}.  The constant
 * GSS_C_NT_HOSTBASED_SERVICE should be initialized
 * to point to that gss_OID_desc.
 */
gss_OID_desc GSSAPI_LIB_VARIABLE __gss_c_nt_hostbased_service_oid_desc =
    {10, rk_UNCONST("\x2a\x86\x48\x86\xf7\x12" "\x01\x02\x01\x04")};

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {6, (void *)"\x2b\x06\01\x05\x06\x03"},
 * corresponding to an object identifier value of
 * {1(iso), 3(org), 6(dod), 1(internet), 5(security),
 * 6(nametypes), 3(gss-anonymous-name)}.  The constant
 * and GSS_C_NT_ANONYMOUS should be initialized to point
 * to that gss_OID_desc.
 */

gss_OID_desc GSSAPI_LIB_VARIABLE __gss_c_nt_anonymous_oid_desc =
    {6, rk_UNCONST("\x2b\x06\01\x05\x06\x03")};

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {6, (void *)"\x2b\x06\x01\x05\x06\x04"},
 * corresponding to an object-identifier value of
 * {1(iso), 3(org), 6(dod), 1(internet), 5(security),
 * 6(nametypes), 4(gss-api-exported-name)}.  The constant
 * GSS_C_NT_EXPORT_NAME should be initialized to point
 * to that gss_OID_desc.
 */

gss_OID_desc GSSAPI_LIB_VARIABLE __gss_c_nt_export_name_oid_desc =
    {6, rk_UNCONST("\x2b\x06\x01\x05\x06\x04") };

/*
 *   This name form shall be represented by the Object Identifier {iso(1)
 *   member-body(2) United States(840) mit(113554) infosys(1) gssapi(2)
 *   krb5(2) krb5_name(1)}.  The recommended symbolic name for this type
 *   is "GSS_KRB5_NT_PRINCIPAL_NAME".
 */

gss_OID_desc GSSAPI_LIB_VARIABLE __gss_krb5_nt_principal_name_oid_desc =
    {10, rk_UNCONST("\x2a\x86\x48\x86\xf7\x12\x01\x02\x02\x01") };

/*
 * draft-ietf-cat-iakerb-09, IAKERB:
 *   The mechanism ID for IAKERB proxy GSS-API Kerberos, in accordance
 *   with the mechanism proposed by SPNEGO [7] for negotiating protocol
 *   variations, is:  {iso(1) org(3) dod(6) internet(1) security(5)
 *   mechanisms(5) iakerb(10) iakerbProxyProtocol(1)}.  The proposed
 *   mechanism ID for IAKERB minimum messages GSS-API Kerberos, in
 *   accordance with the mechanism proposed by SPNEGO for negotiating
 *   protocol variations, is: {iso(1) org(3) dod(6) internet(1)
 *   security(5) mechanisms(5) iakerb(10)
 *   iakerbMinimumMessagesProtocol(2)}.
 */

gss_OID_desc GSSAPI_LIB_VARIABLE  __gss_iakerb_proxy_mechanism_oid_desc =
    {7, rk_UNCONST("\x2b\x06\x01\x05\x05\x0a\x01")};

gss_OID_desc GSSAPI_LIB_VARIABLE __gss_iakerb_min_msg_mechanism_oid_desc =
    {7, rk_UNCONST("\x2b\x06\x01\x05\x05\x0a\x02") };

/*
 * Context for krb5 calls.
 */

#if 0
static gss_mo_desc krb5_mo[] = {
    {
	GSS_C_MA_SASL_MECH_NAME,
	GSS_MO_MA,
	"SASL mech name",
	rk_UNCONST("GS2-KRB5"),
	_gss_mo_get_ctx_as_string,
	NULL
    },
    {
	GSS_C_MA_MECH_NAME,
	GSS_MO_MA,
	"Mechanism name",
	rk_UNCONST("KRB5"),
	_gss_mo_get_ctx_as_string,
	NULL
    },
    {
	GSS_C_MA_MECH_DESCRIPTION,
	GSS_MO_MA,
	"Mechanism description",
	rk_UNCONST("Heimdal Kerberos 5 mech"),
	_gss_mo_get_ctx_as_string,
	NULL
    },
    {
	GSS_C_MA_MECH_CONCRETE,
	GSS_MO_MA
    },
    {
	GSS_C_MA_ITOK_FRAMED,
	GSS_MO_MA
    },
    {
	GSS_C_MA_AUTH_INIT,
	GSS_MO_MA
    },
    {
	GSS_C_MA_AUTH_TARG,
	GSS_MO_MA
    },
    {
	GSS_C_MA_AUTH_INIT_ANON,
	GSS_MO_MA
    },
    {
	GSS_C_MA_DELEG_CRED,
	GSS_MO_MA
    },
    {
	GSS_C_MA_INTEG_PROT,
	GSS_MO_MA
    },
    {
	GSS_C_MA_CONF_PROT,
	GSS_MO_MA
    },
    {
	GSS_C_MA_MIC,
	GSS_MO_MA
    },
    {
	GSS_C_MA_WRAP,
	GSS_MO_MA
    },
    {
	GSS_C_MA_PROT_READY,
	GSS_MO_MA
    },
    {
	GSS_C_MA_REPLAY_DET,
	GSS_MO_MA
    },
    {
	GSS_C_MA_OOS_DET,
	GSS_MO_MA
    },
    {
	GSS_C_MA_CBINDINGS,
	GSS_MO_MA
    },
    {
	GSS_C_MA_PFS,
	GSS_MO_MA
    },
    {
	GSS_C_MA_CTX_TRANS,
	GSS_MO_MA
    }
};
#endif

/*
 *
 */

static gssapi_mech_interface_desc krb5_mech = {
    GMI_VERSION,
    "kerberos 5",
    {9, rk_UNCONST("\x2a\x86\x48\x86\xf7\x12\x01\x02\x02") },
    0,
    _gsskrb5_acquire_cred,
    _gsskrb5_release_cred,
    _gsskrb5_init_sec_context,
    _gsskrb5_accept_sec_context,
    _gsskrb5_process_context_token,
    _gsskrb5_delete_sec_context,
    _gsskrb5_context_time,
    _gsskrb5_get_mic,
    _gsskrb5_verify_mic,
    _gsskrb5_wrap,
    _gsskrb5_unwrap,
    _gsskrb5_display_status,
    _gsskrb5_indicate_mechs,
    _gsskrb5_compare_name,
    _gsskrb5_display_name,
    _gsskrb5_import_name,
    _gsskrb5_export_name,
    _gsskrb5_release_name,
    _gsskrb5_inquire_cred,
    _gsskrb5_inquire_context,
    _gsskrb5_wrap_size_limit,
    _gsskrb5_add_cred,
    _gsskrb5_inquire_cred_by_mech,
    _gsskrb5_export_sec_context,
    _gsskrb5_import_sec_context,
    _gsskrb5_inquire_names_for_mech,
    _gsskrb5_inquire_mechs_for_name,
    _gsskrb5_canonicalize_name,
    _gsskrb5_duplicate_name,
    _gsskrb5_inquire_sec_context_by_oid,
    _gsskrb5_inquire_cred_by_oid,
    _gsskrb5_set_sec_context_option,
    _gsskrb5_set_cred_option,
    _gsskrb5_pseudo_random,
#if 0
    _gk_wrap_iov,
    _gk_unwrap_iov,
    _gk_wrap_iov_length,
#else
    NULL,
    NULL,
    NULL,
#endif
    _gsskrb5_store_cred,
    _gsskrb5_export_cred,
    _gsskrb5_import_cred,
    _gsskrb5_acquire_cred_ext,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
#if 0
    krb5_mo,
    sizeof(krb5_mo) / sizeof(krb5_mo[0]),
#else
    NULL,
    0,
#endif
    _gsskrb5_pname_to_uid,
    _gsskrb5_authorize_localname,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

gssapi_mech_interface
__gss_krb5_initialize(void)
{
    return &krb5_mech;
}
