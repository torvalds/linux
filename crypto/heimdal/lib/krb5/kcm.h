/*
 * Copyright (c) 2005, PADL Software Pty Ltd.
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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

#ifndef __KCM_H__
#define __KCM_H__

/*
 * KCM protocol definitions
 */

#define KCM_PROTOCOL_VERSION_MAJOR	2
#define KCM_PROTOCOL_VERSION_MINOR	0

typedef unsigned char kcmuuid_t[16];

typedef enum kcm_operation {
    KCM_OP_NOOP,
    KCM_OP_GET_NAME,
    KCM_OP_RESOLVE,
    KCM_OP_GEN_NEW,
    KCM_OP_INITIALIZE,
    KCM_OP_DESTROY,
    KCM_OP_STORE,
    KCM_OP_RETRIEVE,
    KCM_OP_GET_PRINCIPAL,
    KCM_OP_GET_CRED_UUID_LIST,
    KCM_OP_GET_CRED_BY_UUID,
    KCM_OP_REMOVE_CRED,
    KCM_OP_SET_FLAGS,
    KCM_OP_CHOWN,
    KCM_OP_CHMOD,
    KCM_OP_GET_INITIAL_TICKET,
    KCM_OP_GET_TICKET,
    KCM_OP_MOVE_CACHE,
    KCM_OP_GET_CACHE_UUID_LIST,
    KCM_OP_GET_CACHE_BY_UUID,
    KCM_OP_GET_DEFAULT_CACHE,
    KCM_OP_SET_DEFAULT_CACHE,
    KCM_OP_GET_KDC_OFFSET,
    KCM_OP_SET_KDC_OFFSET,
    /* NTLM operations */
    KCM_OP_ADD_NTLM_CRED,
    KCM_OP_HAVE_NTLM_CRED,
    KCM_OP_DEL_NTLM_CRED,
    KCM_OP_DO_NTLM_AUTH,
    KCM_OP_GET_NTLM_USER_LIST,
    KCM_OP_MAX
} kcm_operation;

#define _PATH_KCM_SOCKET      "/var/run/.kcm_socket"
#define _PATH_KCM_DOOR      "/var/run/.kcm_door"

#define KCM_NTLM_FLAG_SESSIONKEY 1
#define KCM_NTLM_FLAG_NTLM2_SESSION 2
#define KCM_NTLM_FLAG_KEYEX 4
#define KCM_NTLM_FLAG_AV_GUEST 8

#endif /* __KCM_H__ */

