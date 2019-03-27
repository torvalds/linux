/*
 * Copyright (c) 1997 - 2007 Kungliga Tekniska Högskolan
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

#ifndef GSSAPI_GSSAPI_H_
#define GSSAPI_GSSAPI_H_

/*
 * First, include stddef.h to get size_t defined.
 */
#include <stddef.h>

#include <krb5-types.h>

#ifndef BUILD_GSSAPI_LIB
#if defined(_WIN32)
#define GSSAPI_LIB_FUNCTION __declspec(dllimport)
#define GSSAPI_LIB_CALL     __stdcall
#define GSSAPI_LIB_VARIABLE __declspec(dllimport)
#else
#define GSSAPI_LIB_FUNCTION
#define GSSAPI_LIB_CALL
#define GSSAPI_LIB_VARIABLE
#endif
#endif

#ifndef GSSAPI_DEPRECATED_FUNCTION
#if defined(__GNUC__) && ((__GNUC__ > 3) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1 )))
#define GSSAPI_DEPRECATED_FUNCTION(X) __attribute__((deprecated))
#else
#define GSSAPI_DEPRECATED_FUNCTION(X)
#endif
#endif

/* Compatiblity with MIT Kerberos on the Mac */
#if defined(__APPLE__) && (defined(__ppc__) || defined(__ppc64__) || defined(__i386__) || defined(__x86_64__))
#pragma pack(push,2)
#endif

#ifdef __cplusplus
#define GSSAPI_CPP_START	extern "C" {
#define GSSAPI_CPP_END		}
#else
#define GSSAPI_CPP_START
#define GSSAPI_CPP_END
#endif

#ifdef _WIN32
#define GSSAPI_CALLCONV __stdcall
#else
#define GSSAPI_CALLCONV
#endif

/*
 * Now define the three implementation-dependent types.
 */

typedef uint32_t OM_uint32;
typedef uint64_t OM_uint64;

typedef uint32_t gss_uint32;

struct gss_name_t_desc_struct;
typedef struct gss_name_t_desc_struct *gss_name_t;
typedef const struct gss_name_t_desc_struct *gss_const_name_t;

struct gss_ctx_id_t_desc_struct;
typedef struct gss_ctx_id_t_desc_struct *gss_ctx_id_t;
typedef const struct gss_ctx_id_t_desc_struct gss_const_ctx_id_t;

typedef struct gss_OID_desc_struct {
      OM_uint32 length;
      void      *elements;
} gss_OID_desc, *gss_OID;
typedef const gss_OID_desc * gss_const_OID;

typedef struct gss_OID_set_desc_struct  {
      size_t     count;
      gss_OID    elements;
} gss_OID_set_desc, *gss_OID_set;
typedef const gss_OID_set_desc * gss_const_OID_set;

typedef int gss_cred_usage_t;

struct gss_cred_id_t_desc_struct;
typedef struct gss_cred_id_t_desc_struct *gss_cred_id_t;
typedef const struct gss_cred_id_t_desc_struct *gss_const_cred_id_t;

typedef struct gss_buffer_desc_struct {
      size_t length;
      void *value;
} gss_buffer_desc, *gss_buffer_t;
typedef const gss_buffer_desc * gss_const_buffer_t;

typedef struct gss_channel_bindings_struct {
      OM_uint32 initiator_addrtype;
      gss_buffer_desc initiator_address;
      OM_uint32 acceptor_addrtype;
      gss_buffer_desc acceptor_address;
      gss_buffer_desc application_data;
} *gss_channel_bindings_t;
typedef const struct gss_channel_bindings_struct *gss_const_channel_bindings_t;

/* GGF extension data types */
typedef struct gss_buffer_set_desc_struct {
      size_t count;
      gss_buffer_desc *elements;
} gss_buffer_set_desc, *gss_buffer_set_t;

typedef struct gss_iov_buffer_desc_struct {
    OM_uint32 type;
    gss_buffer_desc buffer;
} gss_iov_buffer_desc, *gss_iov_buffer_t;

/*
 * For now, define a QOP-type as an OM_uint32
 */
typedef OM_uint32 gss_qop_t;



/*
 * Flag bits for context-level services.
 */
#define GSS_C_DELEG_FLAG 1
#define GSS_C_MUTUAL_FLAG 2
#define GSS_C_REPLAY_FLAG 4
#define GSS_C_SEQUENCE_FLAG 8
#define GSS_C_CONF_FLAG 16
#define GSS_C_INTEG_FLAG 32
#define GSS_C_ANON_FLAG 64
#define GSS_C_PROT_READY_FLAG 128
#define GSS_C_TRANS_FLAG 256

#define GSS_C_DCE_STYLE 4096
#define GSS_C_IDENTIFY_FLAG 8192
#define GSS_C_EXTENDED_ERROR_FLAG 16384
#define GSS_C_DELEG_POLICY_FLAG 32768

/*
 * Credential usage options
 */
#define GSS_C_BOTH 0
#define GSS_C_INITIATE 1
#define GSS_C_ACCEPT 2

/*
 * Status code types for gss_display_status
 */
#define GSS_C_GSS_CODE 1
#define GSS_C_MECH_CODE 2

/*
 * The constant definitions for channel-bindings address families
 */
#define GSS_C_AF_UNSPEC     0
#define GSS_C_AF_LOCAL      1
#define GSS_C_AF_INET       2
#define GSS_C_AF_IMPLINK    3
#define GSS_C_AF_PUP        4
#define GSS_C_AF_CHAOS      5
#define GSS_C_AF_NS         6
#define GSS_C_AF_NBS        7
#define GSS_C_AF_ECMA       8
#define GSS_C_AF_DATAKIT    9
#define GSS_C_AF_CCITT      10
#define GSS_C_AF_SNA        11
#define GSS_C_AF_DECnet     12
#define GSS_C_AF_DLI        13
#define GSS_C_AF_LAT        14
#define GSS_C_AF_HYLINK     15
#define GSS_C_AF_APPLETALK  16
#define GSS_C_AF_BSC        17
#define GSS_C_AF_DSS        18
#define GSS_C_AF_OSI        19
#define GSS_C_AF_X25        21
#define GSS_C_AF_INET6	    24

#define GSS_C_AF_NULLADDR   255

/*
 * Various Null values
 */
#define GSS_C_NO_NAME ((gss_name_t) 0)
#define GSS_C_NO_BUFFER ((gss_buffer_t) 0)
#define GSS_C_NO_BUFFER_SET ((gss_buffer_set_t) 0)
#define GSS_C_NO_OID ((gss_OID) 0)
#define GSS_C_NO_OID_SET ((gss_OID_set) 0)
#define GSS_C_NO_CONTEXT ((gss_ctx_id_t) 0)
#define GSS_C_NO_CREDENTIAL ((gss_cred_id_t) 0)
#define GSS_C_NO_CHANNEL_BINDINGS ((gss_channel_bindings_t) 0)
#define GSS_C_EMPTY_BUFFER {0, NULL}
#define GSS_C_NO_IOV_BUFFER ((gss_iov_buffer_t)0)

/*
 * Some alternate names for a couple of the above
 * values.  These are defined for V1 compatibility.
 */
#define GSS_C_NULL_OID GSS_C_NO_OID
#define GSS_C_NULL_OID_SET GSS_C_NO_OID_SET

/*
 * Define the default Quality of Protection for per-message
 * services.  Note that an implementation that offers multiple
 * levels of QOP may define GSS_C_QOP_DEFAULT to be either zero
 * (as done here) to mean "default protection", or to a specific
 * explicit QOP value.  However, a value of 0 should always be
 * interpreted by a GSSAPI implementation as a request for the
 * default protection level.
 */
#define GSS_C_QOP_DEFAULT 0

#define GSS_KRB5_CONF_C_QOP_DES		0x0100
#define GSS_KRB5_CONF_C_QOP_DES3_KD	0x0200

/*
 * Expiration time of 2^32-1 seconds means infinite lifetime for a
 * credential or security context
 */
#define GSS_C_INDEFINITE 0xfffffffful

/*
 * Type of gss_wrap_iov()/gss_unwrap_iov().
 */

#define GSS_IOV_BUFFER_TYPE_EMPTY 0
#define GSS_IOV_BUFFER_TYPE_DATA 1
#define GSS_IOV_BUFFER_TYPE_HEADER 2
#define GSS_IOV_BUFFER_TYPE_MECH_PARAMS 3

#define GSS_IOV_BUFFER_TYPE_TRAILER 7
#define GSS_IOV_BUFFER_TYPE_PADDING 9
#define GSS_IOV_BUFFER_TYPE_STREAM 10
#define GSS_IOV_BUFFER_TYPE_SIGN_ONLY 11

#define GSS_IOV_BUFFER_TYPE_FLAG_MASK		0xffff0000
#define GSS_IOV_BUFFER_FLAG_ALLOCATE		0x00010000
#define GSS_IOV_BUFFER_FLAG_ALLOCATED		0x00020000

#define GSS_IOV_BUFFER_TYPE_FLAG_ALLOCATE	0x00010000 /* old name */
#define GSS_IOV_BUFFER_TYPE_FLAG_ALLOCATED	0x00020000 /* old name */

#define GSS_IOV_BUFFER_TYPE(_t) ((_t) & ~GSS_IOV_BUFFER_TYPE_FLAG_MASK)
#define GSS_IOV_BUFFER_FLAGS(_t) ((_t) & GSS_IOV_BUFFER_TYPE_FLAG_MASK)

GSSAPI_CPP_START

#include <gssapi/gssapi_oid.h>

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
extern GSSAPI_LIB_VARIABLE gss_OID_desc __gss_c_nt_user_name_oid_desc;
#define GSS_C_NT_USER_NAME (&__gss_c_nt_user_name_oid_desc)

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
extern GSSAPI_LIB_VARIABLE gss_OID_desc __gss_c_nt_machine_uid_name_oid_desc;
#define GSS_C_NT_MACHINE_UID_NAME (&__gss_c_nt_machine_uid_name_oid_desc)

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
extern GSSAPI_LIB_VARIABLE gss_OID_desc __gss_c_nt_string_uid_name_oid_desc;
#define GSS_C_NT_STRING_UID_NAME (&__gss_c_nt_string_uid_name_oid_desc)

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
extern GSSAPI_LIB_VARIABLE gss_OID_desc __gss_c_nt_hostbased_service_x_oid_desc;
#define GSS_C_NT_HOSTBASED_SERVICE_X (&__gss_c_nt_hostbased_service_x_oid_desc)

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
extern GSSAPI_LIB_VARIABLE gss_OID_desc __gss_c_nt_hostbased_service_oid_desc;
#define GSS_C_NT_HOSTBASED_SERVICE (&__gss_c_nt_hostbased_service_oid_desc)

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
extern GSSAPI_LIB_VARIABLE gss_OID_desc __gss_c_nt_anonymous_oid_desc;
#define GSS_C_NT_ANONYMOUS (&__gss_c_nt_anonymous_oid_desc)

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
extern GSSAPI_LIB_VARIABLE gss_OID_desc __gss_c_nt_export_name_oid_desc;
#define GSS_C_NT_EXPORT_NAME (&__gss_c_nt_export_name_oid_desc)

/* Major status codes */

#define GSS_S_COMPLETE 0

/*
 * Some "helper" definitions to make the status code macros obvious.
 */
#define GSS_C_CALLING_ERROR_OFFSET 24
#define GSS_C_ROUTINE_ERROR_OFFSET 16
#define GSS_C_SUPPLEMENTARY_OFFSET 0
#define GSS_C_CALLING_ERROR_MASK 0377ul
#define GSS_C_ROUTINE_ERROR_MASK 0377ul
#define GSS_C_SUPPLEMENTARY_MASK 0177777ul

/*
 * The macros that test status codes for error conditions.
 * Note that the GSS_ERROR() macro has changed slightly from
 * the V1 GSSAPI so that it now evaluates its argument
 * only once.
 */
#define GSS_CALLING_ERROR(x) \
  (x & (GSS_C_CALLING_ERROR_MASK << GSS_C_CALLING_ERROR_OFFSET))
#define GSS_ROUTINE_ERROR(x) \
  (x & (GSS_C_ROUTINE_ERROR_MASK << GSS_C_ROUTINE_ERROR_OFFSET))
#define GSS_SUPPLEMENTARY_INFO(x) \
  (x & (GSS_C_SUPPLEMENTARY_MASK << GSS_C_SUPPLEMENTARY_OFFSET))
#define GSS_ERROR(x) \
  (x & ((GSS_C_CALLING_ERROR_MASK << GSS_C_CALLING_ERROR_OFFSET) | \
        (GSS_C_ROUTINE_ERROR_MASK << GSS_C_ROUTINE_ERROR_OFFSET)))

/*
 * Now the actual status code definitions
 */

/*
 * Calling errors:
 */
#define GSS_S_CALL_INACCESSIBLE_READ \
                             (1ul << GSS_C_CALLING_ERROR_OFFSET)
#define GSS_S_CALL_INACCESSIBLE_WRITE \
                             (2ul << GSS_C_CALLING_ERROR_OFFSET)
#define GSS_S_CALL_BAD_STRUCTURE \
                             (3ul << GSS_C_CALLING_ERROR_OFFSET)

/*
 * Routine errors:
 */
#define GSS_S_BAD_MECH (1ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_NAME (2ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_NAMETYPE (3ul << GSS_C_ROUTINE_ERROR_OFFSET)

#define GSS_S_BAD_BINDINGS (4ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_STATUS (5ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_SIG (6ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_MIC GSS_S_BAD_SIG
#define GSS_S_NO_CRED (7ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_NO_CONTEXT (8ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_DEFECTIVE_TOKEN (9ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_DEFECTIVE_CREDENTIAL (10ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_CREDENTIALS_EXPIRED (11ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_CONTEXT_EXPIRED (12ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_FAILURE (13ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_QOP (14ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_UNAUTHORIZED (15ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_UNAVAILABLE (16ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_DUPLICATE_ELEMENT (17ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_NAME_NOT_MN (18ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_MECH_ATTR (19ul << GSS_C_ROUTINE_ERROR_OFFSET)

/*
 * Apparently awating spec fix.
 */
#define GSS_S_CRED_UNAVAIL GSS_S_FAILURE

/*
 * Supplementary info bits:
 */
#define GSS_S_CONTINUE_NEEDED (1ul << (GSS_C_SUPPLEMENTARY_OFFSET + 0))
#define GSS_S_DUPLICATE_TOKEN (1ul << (GSS_C_SUPPLEMENTARY_OFFSET + 1))
#define GSS_S_OLD_TOKEN (1ul << (GSS_C_SUPPLEMENTARY_OFFSET + 2))
#define GSS_S_UNSEQ_TOKEN (1ul << (GSS_C_SUPPLEMENTARY_OFFSET + 3))
#define GSS_S_GAP_TOKEN (1ul << (GSS_C_SUPPLEMENTARY_OFFSET + 4))

/*
 * Finally, function prototypes for the GSS-API routines.
 */

#define GSS_C_OPTION_MASK 0xffff
#define GSS_C_CRED_NO_UI  0x10000

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_acquire_cred
           (OM_uint32 * /*minor_status*/,
            const gss_name_t /*desired_name*/,
            OM_uint32 /*time_req*/,
            const gss_OID_set /*desired_mechs*/,
            gss_cred_usage_t /*cred_usage*/,
            gss_cred_id_t * /*output_cred_handle*/,
            gss_OID_set * /*actual_mechs*/,
            OM_uint32 * /*time_rec*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_release_cred
           (OM_uint32 * /*minor_status*/,
            gss_cred_id_t * /*cred_handle*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_init_sec_context
           (OM_uint32 * /*minor_status*/,
            const gss_cred_id_t /*initiator_cred_handle*/,
            gss_ctx_id_t * /*context_handle*/,
            const gss_name_t /*target_name*/,
            const gss_OID /*mech_type*/,
            OM_uint32 /*req_flags*/,
            OM_uint32 /*time_req*/,
            const gss_channel_bindings_t /*input_chan_bindings*/,
            const gss_buffer_t /*input_token*/,
            gss_OID * /*actual_mech_type*/,
            gss_buffer_t /*output_token*/,
            OM_uint32 * /*ret_flags*/,
            OM_uint32 * /*time_rec*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_accept_sec_context
           (OM_uint32 * /*minor_status*/,
            gss_ctx_id_t * /*context_handle*/,
            const gss_cred_id_t /*acceptor_cred_handle*/,
            const gss_buffer_t /*input_token_buffer*/,
            const gss_channel_bindings_t /*input_chan_bindings*/,
            gss_name_t * /*src_name*/,
            gss_OID * /*mech_type*/,
            gss_buffer_t /*output_token*/,
            OM_uint32 * /*ret_flags*/,
            OM_uint32 * /*time_rec*/,
            gss_cred_id_t * /*delegated_cred_handle*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_process_context_token
           (OM_uint32 * /*minor_status*/,
            const gss_ctx_id_t /*context_handle*/,
            const gss_buffer_t /*token_buffer*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_delete_sec_context
           (OM_uint32 * /*minor_status*/,
            gss_ctx_id_t * /*context_handle*/,
            gss_buffer_t /*output_token*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_context_time
           (OM_uint32 * /*minor_status*/,
            const gss_ctx_id_t /*context_handle*/,
            OM_uint32 * /*time_rec*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_get_mic
           (OM_uint32 * /*minor_status*/,
            const gss_ctx_id_t /*context_handle*/,
            gss_qop_t /*qop_req*/,
            const gss_buffer_t /*message_buffer*/,
            gss_buffer_t /*message_token*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_verify_mic
           (OM_uint32 * /*minor_status*/,
            const gss_ctx_id_t /*context_handle*/,
            const gss_buffer_t /*message_buffer*/,
            const gss_buffer_t /*token_buffer*/,
            gss_qop_t * /*qop_state*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_wrap
           (OM_uint32 * /*minor_status*/,
            const gss_ctx_id_t /*context_handle*/,
            int /*conf_req_flag*/,
            gss_qop_t /*qop_req*/,
            const gss_buffer_t /*input_message_buffer*/,
            int * /*conf_state*/,
            gss_buffer_t /*output_message_buffer*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_unwrap
           (OM_uint32 * /*minor_status*/,
            const gss_ctx_id_t /*context_handle*/,
            const gss_buffer_t /*input_message_buffer*/,
            gss_buffer_t /*output_message_buffer*/,
            int * /*conf_state*/,
            gss_qop_t * /*qop_state*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_display_status
           (OM_uint32 * /*minor_status*/,
            OM_uint32 /*status_value*/,
            int /*status_type*/,
            const gss_OID /*mech_type*/,
            OM_uint32 * /*message_context*/,
            gss_buffer_t /*status_string*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_indicate_mechs
           (OM_uint32 * /*minor_status*/,
            gss_OID_set * /*mech_set*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_compare_name
           (OM_uint32 * /*minor_status*/,
            const gss_name_t /*name1*/,
            const gss_name_t /*name2*/,
            int * /*name_equal*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_display_name
           (OM_uint32 * /*minor_status*/,
            const gss_name_t /*input_name*/,
            gss_buffer_t /*output_name_buffer*/,
            gss_OID * /*output_name_type*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_import_name
           (OM_uint32 * /*minor_status*/,
            const gss_buffer_t /*input_name_buffer*/,
            const gss_OID /*input_name_type*/,
            gss_name_t * /*output_name*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_export_name
           (OM_uint32  * /*minor_status*/,
            const gss_name_t /*input_name*/,
            gss_buffer_t /*exported_name*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_release_name
           (OM_uint32 * /*minor_status*/,
            gss_name_t * /*input_name*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_release_buffer
           (OM_uint32 * /*minor_status*/,
            gss_buffer_t /*buffer*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_release_oid_set
           (OM_uint32 * /*minor_status*/,
            gss_OID_set * /*set*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_inquire_cred
           (OM_uint32 * /*minor_status*/,
            const gss_cred_id_t /*cred_handle*/,
            gss_name_t * /*name*/,
            OM_uint32 * /*lifetime*/,
            gss_cred_usage_t * /*cred_usage*/,
            gss_OID_set * /*mechanisms*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_inquire_context (
            OM_uint32 * /*minor_status*/,
            const gss_ctx_id_t /*context_handle*/,
            gss_name_t * /*src_name*/,
            gss_name_t * /*targ_name*/,
            OM_uint32 * /*lifetime_rec*/,
            gss_OID * /*mech_type*/,
            OM_uint32 * /*ctx_flags*/,
            int * /*locally_initiated*/,
            int * /*open_context*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_wrap_size_limit (
            OM_uint32 * /*minor_status*/,
            const gss_ctx_id_t /*context_handle*/,
            int /*conf_req_flag*/,
            gss_qop_t /*qop_req*/,
            OM_uint32 /*req_output_size*/,
            OM_uint32 * /*max_input_size*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_add_cred (
            OM_uint32 * /*minor_status*/,
            const gss_cred_id_t /*input_cred_handle*/,
            const gss_name_t /*desired_name*/,
            const gss_OID /*desired_mech*/,
            gss_cred_usage_t /*cred_usage*/,
            OM_uint32 /*initiator_time_req*/,
            OM_uint32 /*acceptor_time_req*/,
            gss_cred_id_t * /*output_cred_handle*/,
            gss_OID_set * /*actual_mechs*/,
            OM_uint32 * /*initiator_time_rec*/,
            OM_uint32 * /*acceptor_time_rec*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_inquire_cred_by_mech (
            OM_uint32 * /*minor_status*/,
            const gss_cred_id_t /*cred_handle*/,
            const gss_OID /*mech_type*/,
            gss_name_t * /*name*/,
            OM_uint32 * /*initiator_lifetime*/,
            OM_uint32 * /*acceptor_lifetime*/,
            gss_cred_usage_t * /*cred_usage*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_export_sec_context (
            OM_uint32 * /*minor_status*/,
            gss_ctx_id_t * /*context_handle*/,
            gss_buffer_t /*interprocess_token*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_import_sec_context (
            OM_uint32 * /*minor_status*/,
            const gss_buffer_t /*interprocess_token*/,
            gss_ctx_id_t * /*context_handle*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_create_empty_oid_set (
            OM_uint32 * /*minor_status*/,
            gss_OID_set * /*oid_set*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_add_oid_set_member (
            OM_uint32 * /*minor_status*/,
            const gss_OID /*member_oid*/,
            gss_OID_set * /*oid_set*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_test_oid_set_member (
            OM_uint32 * /*minor_status*/,
            const gss_OID /*member*/,
            const gss_OID_set /*set*/,
            int * /*present*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_inquire_names_for_mech (
            OM_uint32 * /*minor_status*/,
            const gss_OID /*mechanism*/,
            gss_OID_set * /*name_types*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_inquire_mechs_for_name (
            OM_uint32 * /*minor_status*/,
            const gss_name_t /*input_name*/,
            gss_OID_set * /*mech_types*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_canonicalize_name (
            OM_uint32 * /*minor_status*/,
            const gss_name_t /*input_name*/,
            const gss_OID /*mech_type*/,
            gss_name_t * /*output_name*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_duplicate_name (
            OM_uint32 * /*minor_status*/,
            const gss_name_t /*src_name*/,
            gss_name_t * /*dest_name*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_duplicate_oid (
	    OM_uint32 * /* minor_status */,
	    gss_OID /* src_oid */,
	    gss_OID * /* dest_oid */
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_release_oid
	(OM_uint32 * /*minor_status*/,
	 gss_OID * /* oid */
	);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_oid_to_str(
	    OM_uint32 * /*minor_status*/,
	    gss_OID /* oid */,
	    gss_buffer_t /* str */
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_inquire_sec_context_by_oid(
	    OM_uint32 * minor_status,
            const gss_ctx_id_t context_handle,
            const gss_OID desired_object,
            gss_buffer_set_t *data_set
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_set_sec_context_option (OM_uint32 *minor_status,
			    gss_ctx_id_t *context_handle,
			    const gss_OID desired_object,
			    const gss_buffer_t value);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_set_cred_option (OM_uint32 *minor_status,
		     gss_cred_id_t *cred_handle,
		     const gss_OID object,
		     const gss_buffer_t value);

GSSAPI_LIB_FUNCTION int GSSAPI_LIB_CALL
gss_oid_equal(gss_const_OID a, gss_const_OID b);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_create_empty_buffer_set
	   (OM_uint32 * minor_status,
	    gss_buffer_set_t *buffer_set);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_add_buffer_set_member
	   (OM_uint32 * minor_status,
	    const gss_buffer_t member_buffer,
	    gss_buffer_set_t *buffer_set);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_release_buffer_set
	   (OM_uint32 * minor_status,
	    gss_buffer_set_t *buffer_set);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_inquire_cred_by_oid(OM_uint32 *minor_status,
	                const gss_cred_id_t cred_handle,
	                const gss_OID desired_object,
	                gss_buffer_set_t *data_set);

/*
 * RFC 4401
 */

#define GSS_C_PRF_KEY_FULL 0
#define GSS_C_PRF_KEY_PARTIAL 1

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_pseudo_random
	(OM_uint32 *minor_status,
	 gss_ctx_id_t context,
	 int prf_key,
	 const gss_buffer_t prf_in,
	 ssize_t desired_output_len,
	 gss_buffer_t prf_out
	);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_store_cred(OM_uint32         * /* minor_status */,
	       gss_cred_id_t     /* input_cred_handle */,
	       gss_cred_usage_t  /* cred_usage */,
	       const gss_OID     /* desired_mech */,
	       OM_uint32         /* overwrite_cred */,
	       OM_uint32         /* default_cred */,
	       gss_OID_set       * /* elements_stored */,
	       gss_cred_usage_t  * /* cred_usage_stored */);


/*
 * Query functions
 */

typedef struct {
    size_t header; /**< size of header */
    size_t trailer; /**< size of trailer */
    size_t max_msg_size; /**< maximum message size */
    size_t buffers; /**< extra GSS_IOV_BUFFER_TYPE_EMPTY buffer to pass */
    size_t blocksize; /**< Specificed optimal size of messages, also
			 is the maximum padding size
			 (GSS_IOV_BUFFER_TYPE_PADDING) */
} gss_context_stream_sizes;

extern gss_OID_desc GSSAPI_LIB_VARIABLE __gss_c_attr_stream_sizes_oid_desc;
#define GSS_C_ATTR_STREAM_SIZES (&__gss_c_attr_stream_sizes_oid_desc)


GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_context_query_attributes(OM_uint32 * /* minor_status */,
			     const gss_ctx_id_t /* context_handle */,
			     const gss_OID /* attribute */,
			     void * /*data*/,
			     size_t /* len */);
/*
 * The following routines are obsolete variants of gss_get_mic,
 * gss_verify_mic, gss_wrap and gss_unwrap.  They should be
 * provided by GSSAPI V2 implementations for backwards
 * compatibility with V1 applications.  Distinct entrypoints
 * (as opposed to #defines) should be provided, both to allow
 * GSSAPI V1 applications to link against GSSAPI V2 implementations,
 * and to retain the slight parameter type differences between the
 * obsolete versions of these routines and their current forms.
 */

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_sign
           (OM_uint32 * /*minor_status*/,
            gss_ctx_id_t /*context_handle*/,
            int /*qop_req*/,
            gss_buffer_t /*message_buffer*/,
            gss_buffer_t /*message_token*/
	    ) GSSAPI_DEPRECATED_FUNCTION("Use gss_get_mic");

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_verify
           (OM_uint32 * /*minor_status*/,
            gss_ctx_id_t /*context_handle*/,
            gss_buffer_t /*message_buffer*/,
            gss_buffer_t /*token_buffer*/,
            int * /*qop_state*/
	    ) GSSAPI_DEPRECATED_FUNCTION("Use gss_verify_mic");

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_seal
           (OM_uint32 * /*minor_status*/,
            gss_ctx_id_t /*context_handle*/,
            int /*conf_req_flag*/,
            int /*qop_req*/,
            gss_buffer_t /*input_message_buffer*/,
            int * /*conf_state*/,
            gss_buffer_t /*output_message_buffer*/
           ) GSSAPI_DEPRECATED_FUNCTION("Use gss_wrap");

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_unseal
           (OM_uint32 * /*minor_status*/,
            gss_ctx_id_t /*context_handle*/,
            gss_buffer_t /*input_message_buffer*/,
            gss_buffer_t /*output_message_buffer*/,
            int * /*conf_state*/,
            int * /*qop_state*/
           )  GSSAPI_DEPRECATED_FUNCTION("Use gss_unwrap");

/**
 *
 */

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_encapsulate_token(gss_const_buffer_t /* input_token */,
		      gss_const_OID /* oid */,
		      gss_buffer_t /* output_token */);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_decapsulate_token(gss_const_buffer_t /* input_token */,
		      gss_const_OID /* oid */,
		      gss_buffer_t /* output_token */);



/*
 * AEAD support
 */

/*
 * GSS_IOV
 */

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_wrap_iov(OM_uint32 *, gss_ctx_id_t, int, gss_qop_t, int *,
	     gss_iov_buffer_desc *, int);


GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_unwrap_iov(OM_uint32 *, gss_ctx_id_t, int *, gss_qop_t *,
	       gss_iov_buffer_desc *, int);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_wrap_iov_length(OM_uint32 *, gss_ctx_id_t, int, gss_qop_t, int *,
		    gss_iov_buffer_desc *, int);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_release_iov_buffer(OM_uint32 *, gss_iov_buffer_desc *, int);


GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_export_cred(OM_uint32 * /* minor_status */,
		gss_cred_id_t /* cred_handle */,
		gss_buffer_t /* cred_token */);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_import_cred(OM_uint32 * /* minor_status */,
		gss_buffer_t /* cred_token */,
		gss_cred_id_t * /* cred_handle */);

/*
 * mech option
 */

GSSAPI_LIB_FUNCTION int GSSAPI_LIB_CALL
gss_mo_set(gss_const_OID mech, gss_const_OID option,
	   int enable, gss_buffer_t value);

GSSAPI_LIB_FUNCTION int GSSAPI_LIB_CALL
gss_mo_get(gss_const_OID mech, gss_const_OID option, gss_buffer_t value);

GSSAPI_LIB_FUNCTION void GSSAPI_LIB_CALL
gss_mo_list(gss_const_OID mech, gss_OID_set *options);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_mo_name(gss_const_OID mech, gss_const_OID options, gss_buffer_t name);

/*
 * SASL glue functions and mech inquire
 */

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_inquire_saslname_for_mech(OM_uint32 *minor_status,
			      const gss_OID desired_mech,
			      gss_buffer_t sasl_mech_name,
			      gss_buffer_t mech_name,
			      gss_buffer_t mech_description);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_inquire_mech_for_saslname(OM_uint32 *minor_status,
			      const gss_buffer_t sasl_mech_name,
			      gss_OID *mech_type);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_indicate_mechs_by_attrs(OM_uint32 * minor_status,
			    gss_const_OID_set desired_mech_attrs,
			    gss_const_OID_set except_mech_attrs,
			    gss_const_OID_set critical_mech_attrs,
			    gss_OID_set *mechs);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_inquire_attrs_for_mech(OM_uint32 * minor_status,
			   gss_const_OID mech,
			   gss_OID_set *mech_attr,
			   gss_OID_set *known_mech_attrs);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_display_mech_attr(OM_uint32 * minor_status,
		      gss_const_OID mech_attr,
		      gss_buffer_t name,
		      gss_buffer_t short_desc,
		      gss_buffer_t long_desc);

/*
 * Solaris compat
 */

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_acquire_cred_with_password
           (OM_uint32 * /*minor_status*/,
            const gss_name_t /*desired_name*/,
            const gss_buffer_t /*password*/,
            OM_uint32 /*time_req*/,
            const gss_OID_set /*desired_mechs*/,
            gss_cred_usage_t /*cred_usage*/,
            gss_cred_id_t * /*output_cred_handle*/,
            gss_OID_set * /*actual_mechs*/,
            OM_uint32 * /*time_rec*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_add_cred_with_password (
            OM_uint32 * /*minor_status*/,
            const gss_cred_id_t /*input_cred_handle*/,
            const gss_name_t /*desired_name*/,
            const gss_OID /*desired_mech*/,
            const gss_buffer_t /*password*/,
            gss_cred_usage_t /*cred_usage*/,
            OM_uint32 /*initiator_time_req*/,
            OM_uint32 /*acceptor_time_req*/,
            gss_cred_id_t * /*output_cred_handle*/,
            gss_OID_set * /*actual_mechs*/,
            OM_uint32 * /*initiator_time_rec*/,
            OM_uint32 * /*acceptor_time_rec*/
           );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_pname_to_uid(
        OM_uint32 *minor,
        const gss_name_t name,
        const gss_OID mech_type,
        uid_t *uidOut);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_authorize_localname(
        OM_uint32 *minor,
        const gss_name_t name,
        const gss_name_t user);

GSSAPI_LIB_FUNCTION int GSSAPI_LIB_CALL
gss_userok(const gss_name_t name,
           const char *user);

extern GSSAPI_LIB_VARIABLE gss_buffer_desc __gss_c_attr_local_login_user;
#define GSS_C_ATTR_LOCAL_LOGIN_USER (&__gss_c_attr_local_login_user)

/*
 * Naming extensions
 */

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_display_name_ext (
    OM_uint32 *,	/* minor_status */
    gss_name_t,		/* name */
    gss_OID,		/* display_as_name_type */
    gss_buffer_t	/* display_name */
    );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_inquire_name (
    OM_uint32 *,	/* minor_status */
    gss_name_t,		/* name */
    int *,		/* name_is_MN */
    gss_OID *,		/* MN_mech */
    gss_buffer_set_t *	/* attrs */
    );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_get_name_attribute (
    OM_uint32 *,	/* minor_status */
    gss_name_t,		/* name */
    gss_buffer_t,	/* attr */
    int *,		/* authenticated */
    int *,		/* complete */
    gss_buffer_t,	/* value */
    gss_buffer_t,	/* display_value */
    int *		/* more */
    );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_set_name_attribute (
    OM_uint32 *,	/* minor_status */
    gss_name_t,		/* name */
    int,		/* complete */
    gss_buffer_t,	/* attr */
    gss_buffer_t	/* value */
    );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_delete_name_attribute (
    OM_uint32 *,	/* minor_status */
    gss_name_t,		/* name */
    gss_buffer_t	/* attr */
    );

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_export_name_composite (
    OM_uint32 *,	/* minor_status */
    gss_name_t,		/* name */
    gss_buffer_t	/* exp_composite_name */
    );

/*
 *
 */

GSSAPI_LIB_FUNCTION const char * GSSAPI_LIB_CALL
gss_oid_to_name(gss_const_OID oid);

GSSAPI_LIB_FUNCTION gss_OID GSSAPI_LIB_CALL
gss_name_to_oid(const char *name);

GSSAPI_CPP_END

#if defined(__APPLE__) && (defined(__ppc__) || defined(__ppc64__) || defined(__i386__) || defined(__x86_64__))
#pragma pack(pop)
#endif

#undef GSSAPI_DEPRECATED_FUNCTION

#endif /* GSSAPI_GSSAPI_H_ */
