/*
 * Copyright (c) 2010, PADL Software Pty Ltd.
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
 * 3. Neither the name of PADL Software nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PADL SOFTWARE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL PADL SOFTWARE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

typedef OM_uint32 GSSAPI_CALLCONV _gss_inquire_saslname_for_mech_t (
	       OM_uint32 *,           /* minor_status */
	       const gss_OID,         /* desired_mech */
	       gss_buffer_t,          /* sasl_mech_name */
	       gss_buffer_t,          /* mech_name */
	       gss_buffer_t           /* mech_description */
	    );

typedef OM_uint32 GSSAPI_CALLCONV _gss_inquire_mech_for_saslname_t (
	       OM_uint32 *,           /* minor_status */
	       const gss_buffer_t,    /* sasl_mech_name */
	       gss_OID *              /* mech_type */
	    );

typedef OM_uint32 GSSAPI_CALLCONV _gss_inquire_attrs_for_mech_t (
	       OM_uint32 *,           /* minor_status */
	       gss_const_OID,         /* mech */
	       gss_OID_set *,         /* mech_attrs */
	       gss_OID_set *          /* known_mech_attrs */
	    );

typedef OM_uint32 GSSAPI_CALLCONV _gss_acquire_cred_with_password_t
	      (OM_uint32 *,            /* minor_status */
	       const gss_name_t,       /* desired_name */
	       const gss_buffer_t,     /* password */
	       OM_uint32,              /* time_req */
	       const gss_OID_set,      /* desired_mechs */
	       gss_cred_usage_t,       /* cred_usage */
	       gss_cred_id_t *,        /* output_cred_handle */
	       gss_OID_set *,          /* actual_mechs */
	       OM_uint32 *             /* time_rec */
	      );

typedef OM_uint32 GSSAPI_CALLCONV _gss_add_cred_with_password_t (
	       OM_uint32 *,            /* minor_status */
	       const gss_cred_id_t,    /* input_cred_handle */
	       const gss_name_t,       /* desired_name */
	       const gss_OID,          /* desired_mech */
	       const gss_buffer_t,     /* password */
	       gss_cred_usage_t,       /* cred_usage */
	       OM_uint32,              /* initiator_time_req */
	       OM_uint32,              /* acceptor_time_req */
	       gss_cred_id_t *,        /* output_cred_handle */
	       gss_OID_set *,          /* actual_mechs */
	       OM_uint32 *,            /* initiator_time_rec */
	       OM_uint32 *             /* acceptor_time_rec */
	      );

/*
 * API-as-SPI compatibility for compatibility with MIT mechanisms;
 * native Heimdal mechanisms should not use these.
 */
struct gss_mech_compat_desc_struct {
	_gss_inquire_saslname_for_mech_t    *gmc_inquire_saslname_for_mech;
	_gss_inquire_mech_for_saslname_t    *gmc_inquire_mech_for_saslname;
	_gss_inquire_attrs_for_mech_t       *gmc_inquire_attrs_for_mech;
	_gss_acquire_cred_with_password_t   *gmc_acquire_cred_with_password;
#if 0
	_gss_add_cred_with_password_t       *gmc_add_cred_with_password;
#endif
};

