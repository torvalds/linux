/*
 * Copyright (c) 2009 Kungliga Tekniska Högskolan
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

/*! @mainpage Heimdal GSS-API Library
 *
 * Heimdal implements the following mechanisms:
 *
 * - Kerberos 5
 * - SPNEGO
 * - NTLM
 *
 * See @ref gssapi_mechs for more describtion about these mechanisms.
 *
 * The project web page: http://www.h5l.org/
 *
 * - @ref gssapi_services_intro
 * - @ref gssapi_mechs
 * - @ref gssapi_api_INvsMN
 */

/**
 * @page gssapi_services_intro Introduction to GSS-API services
 * @section gssapi_services GSS-API services
 *
 * @subsection gssapi_services_context Context creation
 *
 *  - delegation
 *  - mutual authentication
 *  - anonymous
 *  - use per message before context creation has completed
 *
 *  return status:
 *  - support conf
 *  - support int
 *
 * @subsection gssapi_context_flags Context creation flags
 *
 * - GSS_C_DELEG_FLAG
 * - GSS_C_MUTUAL_FLAG
 * - GSS_C_REPLAY_FLAG
 * - GSS_C_SEQUENCE_FLAG
 * - GSS_C_CONF_FLAG
 * - GSS_C_INTEG_FLAG
 * - GSS_C_ANON_FLAG
 * - GSS_C_PROT_READY_FLAG
 * - GSS_C_TRANS_FLAG
 * - GSS_C_DCE_STYLE
 * - GSS_C_IDENTIFY_FLAG
 * - GSS_C_EXTENDED_ERROR_FLAG
 * - GSS_C_DELEG_POLICY_FLAG
 *
 *
 * @subsection gssapi_services_permessage Per-message services
 *
 *  - conf
 *  - int
 *  - message integrity
 *  - replay detection
 *  - out of sequence
 *
 */

/**
 * @page gssapi_mechs_intro GSS-API mechanisms
 * @section gssapi_mechs GSS-API mechanisms
 *
 * - Kerberos 5 - GSS_KRB5_MECHANISM
 * - SPNEGO - GSS_SPNEGO_MECHANISM
 * - NTLM - GSS_NTLM_MECHANISM

 */


/**
 * @page internalVSmechname Internal names and mechanism names
 * @section gssapi_api_INvsMN Name forms
 *
 * There are two forms of name in GSS-API, Internal form and
 * Contiguous string ("flat") form. gss_export_name() and
 * gss_import_name() can be used to convert between the two forms.
 *
 * - The contiguous string form is described by an oid specificing the
 *   type and an octet string. A special form of the contiguous
 *   string form is the exported name object. The exported name
 *   defined for each mechanism, is something that can be stored and
 *   complared later. The exported name is what should be used for
 *   ACLs comparisons.
 *
 * - The Internal form
 *
 *   There is also special form of the Internal Name (IN), and that is
 *   the Mechanism Name (MN). In the mechanism name all the generic
 *   information is stripped of and only contain the information for
 *   one mechanism.  In GSS-API some function return MN and some
 *   require MN as input. Each of these function is marked up as such.
 *
 *
 * Describe relationship between import_name, canonicalize_name,
 * export_name and friends.
 */

/** @defgroup gssapi Heimdal GSS-API functions */
