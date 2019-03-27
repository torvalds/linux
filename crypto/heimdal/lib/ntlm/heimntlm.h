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

/* $Id$ */

#ifndef HEIM_NTLM_H
#define HEIM_NTLM_H

/**
 * Buffer for storing data in the NTLM library. When filled in by the
 * library it should be freed with heim_ntlm_free_buf().
 */
struct ntlm_buf {
    size_t length; /**< length buffer data */
    void *data; /**< pointer to the data itself */
};

#define NTLM_NEG_UNICODE		0x00000001
#define NTLM_NEG_OEM                    0x00000002
#define NTLM_NEG_TARGET			0x00000004
#define NTLM_MBZ9			0x00000008

#define NTLM_NEG_SIGN			0x00000010
#define NTLM_NEG_SEAL			0x00000020
#define NTLM_NEG_DATAGRAM               0x00000040
#define NTLM_NEG_LM_KEY                 0x00000080
#define NTLM_NEG_NTLM			0x00000200
#define NTLM_NEG_ANONYMOUS              0x00000800

#define NTLM_MBZ8			0x00000100
#define NTLM_NEG_NTLM			0x00000200
#define NTLM_NEG_NT_ONLY		0x00000400
#define NTLM_MBZ7			0x00000800 /* anon ? */

#define NTLM_OEM_SUPPLIED_DOMAIN	0x00001000
#define NTLM_OEM_SUPPLIED_WORKSTATION	0x00002000
#define NTLM_MBZ6			0x00004000 /* local call ? */
#define NTLM_NEG_ALWAYS_SIGN		0x00008000

#define NTLM_TARGET_DOMAIN		0x00010000
#define NTLM_TARGET_SERVER		0x00020000

#define NTLM_TARGET_SHARE		0x00040000
#define NTLM_NEG_NTLM2_SESSION		0x00080000
#define NTLM_NEG_NTLM2			0x00080000

#define NTLM_NEG_IDENTIFY		0x00100000
#define NTLM_MBZ5			0x00200000
#define NTLM_NON_NT_SESSION_KEY		0x00400000
#define NTLM_NEG_TARGET_INFO		0x00800000

#define NTLM_MBZ4			0x01000000
#define NTLM_NEG_VERSION		0x02000000
#define NTLM_MBZ3			0x04000000
#define NTLM_MBZ2			0x08000000

#define NTLM_MBZ1			0x10000000
#define NTLM_ENC_128			0x20000000
#define NTLM_NEG_KEYEX			0x40000000
#define NTLM_ENC_56			0x80000000

/**
 * Struct for the NTLM target info, the strings is assumed to be in
 * UTF8.  When filled in by the library it should be freed with
 * heim_ntlm_free_targetinfo().
 */

#define NTLM_TI_AV_FLAG_GUEST		0x00000001

struct ntlm_targetinfo {
    char *servername; /**< */
    char *domainname; /**< */
    char *dnsdomainname; /**< */
    char *dnsservername; /**< */
    char *dnstreename; /**< */
    uint32_t avflags; /**< */
};

/**
 * Struct for the NTLM type1 message info, the strings is assumed to
 * be in UTF8.  When filled in by the library it should be freed with
 * heim_ntlm_free_type1().
 */

struct ntlm_type1 {
    uint32_t flags; /**< */
    char *domain; /**< */
    char *hostname; /**< */
    uint32_t os[2]; /**< */
};

/**
 * Struct for the NTLM type2 message info, the strings is assumed to
 * be in UTF8.  When filled in by the library it should be freed with
 * heim_ntlm_free_type2().
 */

struct ntlm_type2 {
    uint32_t flags; /**< */
    char *targetname; /**< */
    struct ntlm_buf targetinfo; /**< */
    unsigned char challenge[8]; /**< */
    uint32_t context[2]; /**< */
    uint32_t os[2]; /**< */
};

/**
 * Struct for the NTLM type3 message info, the strings is assumed to
 * be in UTF8.  When filled in by the library it should be freed with
 * heim_ntlm_free_type3().
 */

struct ntlm_type3 {
    uint32_t flags; /**< */
    char *username; /**< */
    char *targetname; /**< */
    struct ntlm_buf lm; /**< */
    struct ntlm_buf ntlm; /**< */
    struct ntlm_buf sessionkey; /**< */
    char *ws; /**< */
    uint32_t os[2]; /**< */
};

#include <ntlm_err.h>
#include <heimntlm-protos.h>

#endif /* NTLM_NTLM_H */
