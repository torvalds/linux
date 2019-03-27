/*
 * Copyright (c) 2006 Kungliga Tekniska Högskolan
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
 * 3. Neither the name of KTH nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY KTH AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL KTH OR ITS CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * $Id$
 */

/* missing from tests:
 * - export context
 * - import context
 */

/*
 * wire encodings:
 *   int16: number, 2 bytes, in network order
 *   int32: number, 4 bytes, in network order
 *   length-encoded: [int32 length, data of length bytes]
 *   string: [int32 length, string of length + 1 bytes, includes trailing '\0' ]
 */

enum gssMaggotErrorCodes {
    GSMERR_OK		= 0,
    GSMERR_ERROR,
    GSMERR_CONTINUE_NEEDED,
    GSMERR_INVALID_TOKEN,
    GSMERR_AP_MODIFIED,
    GSMERR_TEST_ISSUE,
    GSMERR_NOT_SUPPORTED
};

/*
 * input:
 *   int32: message OP (enum gssMaggotProtocol)
 *   ...
 *
 * return:   -- on error
 *    int32: not support (GSMERR_NOT_SUPPORTED)
 *
 * return:   -- on existing message OP
 *    int32: support (GSMERR_OK) -- only sent for extensions
 *    ...
 */

#define GSSMAGGOTPROTOCOL 14

enum gssMaggotOp {
    eGetVersionInfo	= 0,
    /*
     * input:
     *   none
     * return:
     *   int32: last version handled
     */
    eGoodBye,
    /*
     * input:
     *   none
     * return:
     *   close socket
     */
    eInitContext,
    /*
     * input:
     *   int32: hContext
     *   int32: hCred
     *   int32: Flags
     *      the lowest 0x7f flags maps directly to GSS-API flags
     *      DELEGATE		0x001
     *      MUTUAL_AUTH		0x002
     *      REPLAY_DETECT	0x004
     *      SEQUENCE_DETECT	0x008
     *      CONFIDENTIALITY	0x010
     *      INTEGRITY		0x020
     *      ANONYMOUS		0x040
     *
     *      FIRST_CALL		0x080
     *
     *      NTLM		0x100
     *      SPNEGO		0x200
     *   length-encoded: targetname
     *   length-encoded: token
     * return:
     *   int32: hNewContextId
     *   int32: gssapi status val
     *   length-encoded: output token
     */
    eAcceptContext,
    /*
     * input:
     *   int32: hContext
     *   int32: Flags		-- unused ?
     *      flags are same as flags for eInitContext
     *   length-encoded: token
     * return:
     *   int32: hNewContextId
     *   int32: gssapi status val
     *   length-encoded: output token
     *   int32: delegation cred id
     */
    eToastResource,
    /*
     * input:
     *   int32: hResource
     * return:
     *   int32: gsm status val
     */
    eAcquireCreds,
    /*
     * input:
     *   string: principal name
     *   string: password
     *   int32: flags
     *      FORWARDABLE		0x001
     *      DEFAULT_CREDS	0x002
     *
     *      NTLM		0x100
     *      SPNEGO		0x200
     * return:
     *   int32: gsm status val
     *   int32: hCred
     */
    eEncrypt,
    /*
     * input:
     *   int32: hContext
     *   int32: flags
     *   int32: seqno		-- unused
     *   length-encode: plaintext
     * return:
     *   int32: gsm status val
     *   length-encode: ciphertext
     */
    eDecrypt,
    /*
     * input:
     *   int32: hContext
     *   int32: flags
     *   int32: seqno		-- unused
     *   length-encode: ciphertext
     * return:
     *   int32: gsm status val
     *   length-encode: plaintext
     */
    eSign,
    /* message same as eEncrypt */
    eVerify,
    /*
     * input:
     *   int32: hContext
     *   int32: flags
     *   int32: seqno		-- unused
     *   length-encode: message
     *   length-encode: signature
     * return:
     *   int32: gsm status val
     */
    eGetVersionAndCapabilities,
    /*
     * return:
     *   int32: protocol version
     *   int32: capability flags */
#define      ISSERVER		0x01
#define      ISKDC		0x02
#define      MS_KERBEROS	0x04
#define      LOGSERVER		0x08
#define      HAS_MONIKER	0x10
    /*   string: version string
     */
    eGetTargetName,
    /*
     * return:
     *   string: target principal name
     */
    eSetLoggingSocket,
    /*
     * input:
     *   int32: hostPort
     * return to the port on the host:
     *   int32: opcode - for example eLogSetMoniker
     */
    eChangePassword,
    /* here ended version 7 of the protocol */
    /*
     * input:
     *   string: principal name
     *   string: old password
     *   string: new password
     * return:
     *   int32: gsm status val
     */
    eSetPasswordSelf,
    /* same as eChangePassword */
    eWrap,
    /* message same as eEncrypt */
    eUnwrap,
    /* message same as eDecrypt */
    eConnectLoggingService2,
    /*
     * return1:
     *   int16: log port number
     *   int32: master log prototocol version (0)
     *
     * wait for master to connect on the master log socket
     *
     * return2:
     *   int32: gsm connection status
     *   int32: maggot log prototocol version (2)
     */
    eGetMoniker,
    /*
     * return:
     *   string: moniker (Nickname the master can refer to maggot)
     */
    eCallExtension,
    /*
     * input:
     *   string: extension name
     *   int32: message id
     * return:
     *   int32: gsm status val
     */
    eAcquirePKInitCreds,
    /*
     * input:
     *   int32: flags
     *   length-encode: certificate (pkcs12 data)
     * return:
     *   int32: hResource
     *   int32: gsm status val (GSMERR_NOT_SUPPORTED)
     */
    /* here ended version 7 of the protocol */
    eWrapExt,
    /*
     * input:
     *   int32: hContext
     *   int32: flags
     *   int32: bflags
     *   length-encode: protocol header
     *   length-encode: plaintext
     *   length-encode: protocol trailer
     * return:
     *   int32: gsm status val
     *   length-encode: ciphertext
     */
    eUnwrapExt,
    /*
     * input:
     *   int32: hContext
     *   int32: flags
     *   int32: bflags
     *   length-encode: protocol header
     *   length-encode: ciphertext
     *   length-encode: protocol trailer
     * return:
     *   int32: gsm status val
     *   length-encode: plaintext
     */
    /* here ended version 8 of the protocol */

    eLastProtocolMessage
};

/* bflags */
#define WRAP_EXP_ONLY_HEADER 1

enum gssMaggotLogOp{
  eLogInfo = 0,
	/*
	string: File
	int32: Line
	string: message
     reply:
  	int32: ackid
	*/
  eLogFailure,
	/*
	string: File
	int32: Line
	string: message
     reply:
  	int32: ackid
	*/
  eLogSetMoniker
	/*
	string: moniker
	*/
};
