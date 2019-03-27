/*
 * Copyright (c) 2006 - 2008 Kungliga Tekniska Högskolan
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

#include "hx_locl.h"
#include "pkcs11.h"
#include <err.h>

static CK_FUNCTION_LIST_PTR func;


static CK_RV
find_object(CK_SESSION_HANDLE session,
	    char *id,
	    CK_OBJECT_CLASS key_class,
	    CK_OBJECT_HANDLE_PTR object)
{
    CK_ULONG object_count;
    CK_RV ret;
    CK_ATTRIBUTE search_data[] = {
	{CKA_ID, id, 0 },
	{CKA_CLASS, &key_class, sizeof(key_class)}
    };
    CK_ULONG num_search_data = sizeof(search_data)/sizeof(search_data[0]);

    search_data[0].ulValueLen = strlen(id);

    ret = (*func->C_FindObjectsInit)(session, search_data, num_search_data);
    if (ret != CKR_OK)
	return ret;

    ret = (*func->C_FindObjects)(session, object, 1, &object_count);
    if (ret != CKR_OK)
	return ret;
    if (object_count == 0) {
	printf("found no object\n");
	return 1;
    }

    ret = (*func->C_FindObjectsFinal)(session);
    if (ret != CKR_OK)
	return ret;

    return CKR_OK;
}

static char *sighash = "hej";
static char signature[1024];


int
main(int argc, char **argv)
{
    CK_SLOT_ID_PTR slot_ids;
    CK_SLOT_ID slot;
    CK_ULONG num_slots;
    CK_RV ret;
    CK_SLOT_INFO slot_info;
    CK_TOKEN_INFO token_info;
    CK_SESSION_HANDLE session;
    CK_OBJECT_HANDLE public, private;

    ret = C_GetFunctionList(&func);
    if (ret != CKR_OK)
	errx(1, "C_GetFunctionList failed: %d", (int)ret);

    (*func->C_Initialize)(NULL_PTR);

    ret = (*func->C_GetSlotList)(FALSE, NULL, &num_slots);
    if (ret != CKR_OK)
	errx(1, "C_GetSlotList1 failed: %d", (int)ret);

    if (num_slots == 0)
	errx(1, "no slots");

    if ((slot_ids = calloc(1, num_slots * sizeof(*slot_ids))) == NULL)
	err(1, "alloc slots failed");

    ret = (*func->C_GetSlotList)(FALSE, slot_ids, &num_slots);
    if (ret != CKR_OK)
	errx(1, "C_GetSlotList2 failed: %d", (int)ret);

    slot = slot_ids[0];
    free(slot_ids);

    ret = (*func->C_GetSlotInfo)(slot, &slot_info);
    if (ret)
	errx(1, "C_GetSlotInfo failed: %d", (int)ret);

    if ((slot_info.flags & CKF_TOKEN_PRESENT) == 0)
	errx(1, "no token present");

    ret = (*func->C_OpenSession)(slot, CKF_SERIAL_SESSION,
				 NULL, NULL, &session);
    if (ret != CKR_OK)
	errx(1, "C_OpenSession failed: %d", (int)ret);

    ret = (*func->C_GetTokenInfo)(slot, &token_info);
    if (ret)
	errx(1, "C_GetTokenInfo1 failed: %d", (int)ret);

    if (token_info.flags & CKF_LOGIN_REQUIRED) {
	ret = (*func->C_Login)(session, CKU_USER,
			       (unsigned char*)"foobar", 6);
	if (ret != CKR_OK)
	    errx(1, "C_Login failed: %d", (int)ret);
    }

    ret = (*func->C_GetTokenInfo)(slot, &token_info);
    if (ret)
	errx(1, "C_GetTokenInfo2 failed: %d", (int)ret);

    if (token_info.flags & CKF_LOGIN_REQUIRED)
	errx(1, "login required, even after C_Login");

    ret = find_object(session, "cert", CKO_PUBLIC_KEY, &public);
    if (ret != CKR_OK)
	errx(1, "find cert failed: %d", (int)ret);
    ret = find_object(session, "cert", CKO_PRIVATE_KEY, &private);
    if (ret != CKR_OK)
	errx(1, "find private key failed: %d", (int)ret);

    {
	CK_ULONG ck_sigsize;
	CK_MECHANISM mechanism;

	memset(&mechanism, 0, sizeof(mechanism));
	mechanism.mechanism = CKM_RSA_PKCS;

	ret = (*func->C_SignInit)(session, &mechanism, private);
	if (ret != CKR_OK)
	    return 1;

	ck_sigsize = sizeof(signature);
	ret = (*func->C_Sign)(session, (CK_BYTE *)sighash, strlen(sighash),
			      (CK_BYTE *)signature, &ck_sigsize);
	if (ret != CKR_OK) {
	    printf("C_Sign failed with: %d\n", (int)ret);
	    return 1;
	}

	ret = (*func->C_VerifyInit)(session, &mechanism, public);
	if (ret != CKR_OK)
	    return 1;

	ret = (*func->C_Verify)(session, (CK_BYTE *)signature, ck_sigsize,
				(CK_BYTE *)sighash, strlen(sighash));
	if (ret != CKR_OK) {
	    printf("message: %d\n", (int)ret);
	    return 1;
	}
    }

#if 0
    {
	CK_ULONG ck_sigsize, outsize;
	CK_MECHANISM mechanism;
	char outdata[1024];

	memset(&mechanism, 0, sizeof(mechanism));
	mechanism.mechanism = CKM_RSA_PKCS;

	ret = (*func->C_EncryptInit)(session, &mechanism, public);
	if (ret != CKR_OK)
	    return 1;

	ck_sigsize = sizeof(signature);
	ret = (*func->C_Encrypt)(session, (CK_BYTE *)sighash, strlen(sighash),
				 (CK_BYTE *)signature, &ck_sigsize);
	if (ret != CKR_OK) {
	    printf("message: %d\n", (int)ret);
	    return 1;
	}

	ret = (*func->C_DecryptInit)(session, &mechanism, private);
	if (ret != CKR_OK)
	    return 1;

	outsize = sizeof(outdata);
	ret = (*func->C_Decrypt)(session, (CK_BYTE *)signature, ck_sigsize,
				 (CK_BYTE *)outdata, &outsize);
	if (ret != CKR_OK) {
	    printf("message: %d\n", (int)ret);
	    return 1;
	}

	if (ct_memcmp(sighash, outdata, strlen(sighash)) != 0)
	    return 1;
    }
#endif

    ret = (*func->C_CloseSession)(session);
    if (ret != CKR_OK)
	return 1;

    (*func->C_Finalize)(NULL_PTR);

    return 0;
}
