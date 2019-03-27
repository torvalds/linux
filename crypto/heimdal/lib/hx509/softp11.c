/*
 * Copyright (c) 2004 - 2008 Kungliga Tekniska Högskolan
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

#define CRYPTOKI_EXPORTS 1

#include "hx_locl.h"
#include "pkcs11.h"

#define OBJECT_ID_MASK		0xfff
#define HANDLE_OBJECT_ID(h)	((h) & OBJECT_ID_MASK)
#define OBJECT_ID(obj)		HANDLE_OBJECT_ID((obj)->object_handle)

#ifndef HAVE_RANDOM
#define random() rand()
#define srandom(s) srand(s)
#endif

#ifdef _WIN32
#include <shlobj.h>
#endif

struct st_attr {
    CK_ATTRIBUTE attribute;
    int secret;
};

struct st_object {
    CK_OBJECT_HANDLE object_handle;
    struct st_attr *attrs;
    int num_attributes;
    hx509_cert cert;
};

static struct soft_token {
    CK_VOID_PTR application;
    CK_NOTIFY notify;
    char *config_file;
    hx509_certs certs;
    struct {
	struct st_object **objs;
	int num_objs;
    } object;
    struct {
	int hardware_slot;
	int app_error_fatal;
	int login_done;
    } flags;
    int open_sessions;
    struct session_state {
	CK_SESSION_HANDLE session_handle;

	struct {
	    CK_ATTRIBUTE *attributes;
	    CK_ULONG num_attributes;
	    int next_object;
	} find;

	int sign_object;
	CK_MECHANISM_PTR sign_mechanism;
	int verify_object;
	CK_MECHANISM_PTR verify_mechanism;
    } state[10];
#define MAX_NUM_SESSION (sizeof(soft_token.state)/sizeof(soft_token.state[0]))
    FILE *logfile;
} soft_token;

static hx509_context context;

static void
application_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (soft_token.flags.app_error_fatal)
	abort();
}

static void
st_logf(const char *fmt, ...)
{
    va_list ap;
    if (soft_token.logfile == NULL)
	return;
    va_start(ap, fmt);
    vfprintf(soft_token.logfile, fmt, ap);
    va_end(ap);
    fflush(soft_token.logfile);
}

static CK_RV
init_context(void)
{
    if (context == NULL) {
	int ret = hx509_context_init(&context);
	if (ret)
	    return CKR_GENERAL_ERROR;
    }
    return CKR_OK;
}

#define INIT_CONTEXT() { CK_RV icret = init_context(); if (icret) return icret; }

static void
snprintf_fill(char *str, size_t size, char fillchar, const char *fmt, ...)
{
    int len;
    va_list ap;
    va_start(ap, fmt);
    len = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    if (len < 0 || (size_t)len > size)
	return;
    while ((size_t)len < size)
	str[len++] = fillchar;
}

#ifndef TEST_APP
#define printf error_use_st_logf
#endif

#define VERIFY_SESSION_HANDLE(s, state)			\
{							\
    CK_RV xret;						\
    xret = verify_session_handle(s, state);		\
    if (xret != CKR_OK) {				\
	/* return CKR_OK */;				\
    }							\
}

static CK_RV
verify_session_handle(CK_SESSION_HANDLE hSession,
		      struct session_state **state)
{
    size_t i;

    for (i = 0; i < MAX_NUM_SESSION; i++){
	if (soft_token.state[i].session_handle == hSession)
	    break;
    }
    if (i == MAX_NUM_SESSION) {
	application_error("use of invalid handle: 0x%08lx\n",
			  (unsigned long)hSession);
	return CKR_SESSION_HANDLE_INVALID;
    }
    if (state)
	*state = &soft_token.state[i];
    return CKR_OK;
}

static CK_RV
object_handle_to_object(CK_OBJECT_HANDLE handle,
			struct st_object **object)
{
    int i = HANDLE_OBJECT_ID(handle);

    *object = NULL;
    if (i >= soft_token.object.num_objs)
	return CKR_ARGUMENTS_BAD;
    if (soft_token.object.objs[i] == NULL)
	return CKR_ARGUMENTS_BAD;
    if (soft_token.object.objs[i]->object_handle != handle)
	return CKR_ARGUMENTS_BAD;
    *object = soft_token.object.objs[i];
    return CKR_OK;
}

static int
attributes_match(const struct st_object *obj,
		 const CK_ATTRIBUTE *attributes,
		 CK_ULONG num_attributes)
{
    CK_ULONG i;
    int j;

    st_logf("attributes_match: %ld\n", (unsigned long)OBJECT_ID(obj));

    for (i = 0; i < num_attributes; i++) {
	int match = 0;
	for (j = 0; j < obj->num_attributes; j++) {
	    if (attributes[i].type == obj->attrs[j].attribute.type &&
		attributes[i].ulValueLen == obj->attrs[j].attribute.ulValueLen &&
		memcmp(attributes[i].pValue, obj->attrs[j].attribute.pValue,
		       attributes[i].ulValueLen) == 0) {
		match = 1;
		break;
	    }
	}
	if (match == 0) {
	    st_logf("type %d attribute have no match\n", attributes[i].type);
	    return 0;
	}
    }
    st_logf("attribute matches\n");
    return 1;
}

static void
print_attributes(const CK_ATTRIBUTE *attributes,
		 CK_ULONG num_attributes)
{
    CK_ULONG i;

    st_logf("find objects: attrs: %lu\n", (unsigned long)num_attributes);

    for (i = 0; i < num_attributes; i++) {
	st_logf("  type: ");
	switch (attributes[i].type) {
	case CKA_TOKEN: {
	    CK_BBOOL *ck_true;
	    if (attributes[i].ulValueLen != sizeof(CK_BBOOL)) {
		application_error("token attribute wrong length\n");
		break;
	    }
	    ck_true = attributes[i].pValue;
	    st_logf("token: %s", *ck_true ? "TRUE" : "FALSE");
	    break;
	}
	case CKA_CLASS: {
	    CK_OBJECT_CLASS *class;
	    if (attributes[i].ulValueLen != sizeof(CK_ULONG)) {
		application_error("class attribute wrong length\n");
		break;
	    }
	    class = attributes[i].pValue;
	    st_logf("class ");
	    switch (*class) {
	    case CKO_CERTIFICATE:
		st_logf("certificate");
		break;
	    case CKO_PUBLIC_KEY:
		st_logf("public key");
		break;
	    case CKO_PRIVATE_KEY:
		st_logf("private key");
		break;
	    case CKO_SECRET_KEY:
		st_logf("secret key");
		break;
	    case CKO_DOMAIN_PARAMETERS:
		st_logf("domain parameters");
		break;
	    default:
		st_logf("[class %lx]", (long unsigned)*class);
		break;
	    }
	    break;
	}
	case CKA_PRIVATE:
	    st_logf("private");
	    break;
	case CKA_LABEL:
	    st_logf("label");
	    break;
	case CKA_APPLICATION:
	    st_logf("application");
	    break;
	case CKA_VALUE:
	    st_logf("value");
	    break;
	case CKA_ID:
	    st_logf("id");
	    break;
	default:
	    st_logf("[unknown 0x%08lx]", (unsigned long)attributes[i].type);
	    break;
	}
	st_logf("\n");
    }
}

static struct st_object *
add_st_object(void)
{
    struct st_object *o, **objs;
    int i;

    o = calloc(1, sizeof(*o));
    if (o == NULL)
	return NULL;

    for (i = 0; i < soft_token.object.num_objs; i++) {
	if (soft_token.object.objs == NULL) {
	    soft_token.object.objs[i] = o;
	    break;
	}
    }
    if (i == soft_token.object.num_objs) {
	objs = realloc(soft_token.object.objs,
		       (soft_token.object.num_objs + 1) * sizeof(soft_token.object.objs[0]));
	if (objs == NULL) {
	    free(o);
	    return NULL;
	}
	soft_token.object.objs = objs;
	soft_token.object.objs[soft_token.object.num_objs++] = o;
    }
    soft_token.object.objs[i]->object_handle =
	(random() & (~OBJECT_ID_MASK)) | i;

    return o;
}

static CK_RV
add_object_attribute(struct st_object *o,
		     int secret,
		     CK_ATTRIBUTE_TYPE type,
		     CK_VOID_PTR pValue,
		     CK_ULONG ulValueLen)
{
    struct st_attr *a;
    int i;

    i = o->num_attributes;
    a = realloc(o->attrs, (i + 1) * sizeof(o->attrs[0]));
    if (a == NULL)
	return CKR_DEVICE_MEMORY;
    o->attrs = a;
    o->attrs[i].secret = secret;
    o->attrs[i].attribute.type = type;
    o->attrs[i].attribute.pValue = malloc(ulValueLen);
    if (o->attrs[i].attribute.pValue == NULL && ulValueLen != 0)
	return CKR_DEVICE_MEMORY;
    memcpy(o->attrs[i].attribute.pValue, pValue, ulValueLen);
    o->attrs[i].attribute.ulValueLen = ulValueLen;
    o->num_attributes++;

    return CKR_OK;
}

static CK_RV
add_pubkey_info(hx509_context hxctx, struct st_object *o,
		CK_KEY_TYPE key_type, hx509_cert cert)
{
    BIGNUM *num;
    CK_BYTE *modulus = NULL;
    size_t modulus_len = 0;
    CK_ULONG modulus_bits = 0;
    CK_BYTE *exponent = NULL;
    size_t exponent_len = 0;

    if (key_type != CKK_RSA)
	return CKR_OK;
    if (_hx509_cert_private_key(cert) == NULL)
	return CKR_OK;

    num = _hx509_private_key_get_internal(context,
					  _hx509_cert_private_key(cert),
					  "rsa-modulus");
    if (num == NULL)
	return CKR_GENERAL_ERROR;
    modulus_bits = BN_num_bits(num);

    modulus_len = BN_num_bytes(num);
    modulus = malloc(modulus_len);
    BN_bn2bin(num, modulus);
    BN_free(num);

    add_object_attribute(o, 0, CKA_MODULUS, modulus, modulus_len);
    add_object_attribute(o, 0, CKA_MODULUS_BITS,
			 &modulus_bits, sizeof(modulus_bits));

    free(modulus);

    num = _hx509_private_key_get_internal(context,
					  _hx509_cert_private_key(cert),
					  "rsa-exponent");
    if (num == NULL)
	return CKR_GENERAL_ERROR;

    exponent_len = BN_num_bytes(num);
    exponent = malloc(exponent_len);
    BN_bn2bin(num, exponent);
    BN_free(num);

    add_object_attribute(o, 0, CKA_PUBLIC_EXPONENT,
			 exponent, exponent_len);

    free(exponent);

    return CKR_OK;
}


struct foo {
    char *label;
    char *id;
};

static int
add_cert(hx509_context hxctx, void *ctx, hx509_cert cert)
{
    static char empty[] = "";
    struct foo *foo = (struct foo *)ctx;
    struct st_object *o = NULL;
    CK_OBJECT_CLASS type;
    CK_BBOOL bool_true = CK_TRUE;
    CK_BBOOL bool_false = CK_FALSE;
    CK_CERTIFICATE_TYPE cert_type = CKC_X_509;
    CK_KEY_TYPE key_type;
    CK_MECHANISM_TYPE mech_type;
    CK_RV ret = CKR_GENERAL_ERROR;
    int hret;
    heim_octet_string cert_data, subject_data, issuer_data, serial_data;

    st_logf("adding certificate\n");

    serial_data.data = NULL;
    serial_data.length = 0;
    cert_data = subject_data = issuer_data = serial_data;

    hret = hx509_cert_binary(hxctx, cert, &cert_data);
    if (hret)
	goto out;

    {
	    hx509_name name;

	    hret = hx509_cert_get_issuer(cert, &name);
	    if (hret)
		goto out;
	    hret = hx509_name_binary(name, &issuer_data);
	    hx509_name_free(&name);
	    if (hret)
		goto out;

	    hret = hx509_cert_get_subject(cert, &name);
	    if (hret)
		goto out;
	    hret = hx509_name_binary(name, &subject_data);
	    hx509_name_free(&name);
	    if (hret)
		goto out;
    }

    {
	AlgorithmIdentifier alg;

	hret = hx509_cert_get_SPKI_AlgorithmIdentifier(context, cert, &alg);
	if (hret) {
	    ret = CKR_DEVICE_MEMORY;
	    goto out;
	}

	key_type = CKK_RSA; /* XXX */

	free_AlgorithmIdentifier(&alg);
    }


    type = CKO_CERTIFICATE;
    o = add_st_object();
    if (o == NULL) {
	ret = CKR_DEVICE_MEMORY;
	goto out;
    }

    o->cert = hx509_cert_ref(cert);

    add_object_attribute(o, 0, CKA_CLASS, &type, sizeof(type));
    add_object_attribute(o, 0, CKA_TOKEN, &bool_true, sizeof(bool_true));
    add_object_attribute(o, 0, CKA_PRIVATE, &bool_false, sizeof(bool_false));
    add_object_attribute(o, 0, CKA_MODIFIABLE, &bool_false, sizeof(bool_false));
    add_object_attribute(o, 0, CKA_LABEL, foo->label, strlen(foo->label));

    add_object_attribute(o, 0, CKA_CERTIFICATE_TYPE, &cert_type, sizeof(cert_type));
    add_object_attribute(o, 0, CKA_ID, foo->id, strlen(foo->id));

    add_object_attribute(o, 0, CKA_SUBJECT, subject_data.data, subject_data.length);
    add_object_attribute(o, 0, CKA_ISSUER, issuer_data.data, issuer_data.length);
    add_object_attribute(o, 0, CKA_SERIAL_NUMBER, serial_data.data, serial_data.length);
    add_object_attribute(o, 0, CKA_VALUE, cert_data.data, cert_data.length);
    add_object_attribute(o, 0, CKA_TRUSTED, &bool_false, sizeof(bool_false));

    st_logf("add cert ok: %lx\n", (unsigned long)OBJECT_ID(o));

    type = CKO_PUBLIC_KEY;
    o = add_st_object();
    if (o == NULL) {
	ret = CKR_DEVICE_MEMORY;
	goto out;
    }
    o->cert = hx509_cert_ref(cert);

    add_object_attribute(o, 0, CKA_CLASS, &type, sizeof(type));
    add_object_attribute(o, 0, CKA_TOKEN, &bool_true, sizeof(bool_true));
    add_object_attribute(o, 0, CKA_PRIVATE, &bool_false, sizeof(bool_false));
    add_object_attribute(o, 0, CKA_MODIFIABLE, &bool_false, sizeof(bool_false));
    add_object_attribute(o, 0, CKA_LABEL, foo->label, strlen(foo->label));

    add_object_attribute(o, 0, CKA_KEY_TYPE, &key_type, sizeof(key_type));
    add_object_attribute(o, 0, CKA_ID, foo->id, strlen(foo->id));
    add_object_attribute(o, 0, CKA_START_DATE, empty, 1); /* XXX */
    add_object_attribute(o, 0, CKA_END_DATE, empty, 1); /* XXX */
    add_object_attribute(o, 0, CKA_DERIVE, &bool_false, sizeof(bool_false));
    add_object_attribute(o, 0, CKA_LOCAL, &bool_false, sizeof(bool_false));
    mech_type = CKM_RSA_X_509;
    add_object_attribute(o, 0, CKA_KEY_GEN_MECHANISM, &mech_type, sizeof(mech_type));

    add_object_attribute(o, 0, CKA_SUBJECT, subject_data.data, subject_data.length);
    add_object_attribute(o, 0, CKA_ENCRYPT, &bool_true, sizeof(bool_true));
    add_object_attribute(o, 0, CKA_VERIFY, &bool_true, sizeof(bool_true));
    add_object_attribute(o, 0, CKA_VERIFY_RECOVER, &bool_false, sizeof(bool_false));
    add_object_attribute(o, 0, CKA_WRAP, &bool_true, sizeof(bool_true));
    add_object_attribute(o, 0, CKA_TRUSTED, &bool_true, sizeof(bool_true));

    add_pubkey_info(hxctx, o, key_type, cert);

    st_logf("add key ok: %lx\n", (unsigned long)OBJECT_ID(o));

    if (hx509_cert_have_private_key(cert)) {
	CK_FLAGS flags;

	type = CKO_PRIVATE_KEY;
	o = add_st_object();
	if (o == NULL) {
	    ret = CKR_DEVICE_MEMORY;
	    goto out;
	}
	o->cert = hx509_cert_ref(cert);

	add_object_attribute(o, 0, CKA_CLASS, &type, sizeof(type));
	add_object_attribute(o, 0, CKA_TOKEN, &bool_true, sizeof(bool_true));
	add_object_attribute(o, 0, CKA_PRIVATE, &bool_true, sizeof(bool_false));
	add_object_attribute(o, 0, CKA_MODIFIABLE, &bool_false, sizeof(bool_false));
	add_object_attribute(o, 0, CKA_LABEL, foo->label, strlen(foo->label));

	add_object_attribute(o, 0, CKA_KEY_TYPE, &key_type, sizeof(key_type));
	add_object_attribute(o, 0, CKA_ID, foo->id, strlen(foo->id));
	add_object_attribute(o, 0, CKA_START_DATE, empty, 1); /* XXX */
	add_object_attribute(o, 0, CKA_END_DATE, empty, 1); /* XXX */
	add_object_attribute(o, 0, CKA_DERIVE, &bool_false, sizeof(bool_false));
	add_object_attribute(o, 0, CKA_LOCAL, &bool_false, sizeof(bool_false));
	mech_type = CKM_RSA_X_509;
	add_object_attribute(o, 0, CKA_KEY_GEN_MECHANISM, &mech_type, sizeof(mech_type));

	add_object_attribute(o, 0, CKA_SUBJECT, subject_data.data, subject_data.length);
	add_object_attribute(o, 0, CKA_SENSITIVE, &bool_true, sizeof(bool_true));
	add_object_attribute(o, 0, CKA_SECONDARY_AUTH, &bool_false, sizeof(bool_true));
	flags = 0;
	add_object_attribute(o, 0, CKA_AUTH_PIN_FLAGS, &flags, sizeof(flags));

	add_object_attribute(o, 0, CKA_DECRYPT, &bool_true, sizeof(bool_true));
	add_object_attribute(o, 0, CKA_SIGN, &bool_true, sizeof(bool_true));
	add_object_attribute(o, 0, CKA_SIGN_RECOVER, &bool_false, sizeof(bool_false));
	add_object_attribute(o, 0, CKA_UNWRAP, &bool_true, sizeof(bool_true));
	add_object_attribute(o, 0, CKA_EXTRACTABLE, &bool_true, sizeof(bool_true));
	add_object_attribute(o, 0, CKA_NEVER_EXTRACTABLE, &bool_false, sizeof(bool_false));

	add_pubkey_info(hxctx, o, key_type, cert);
    }

    ret = CKR_OK;
 out:
    if (ret != CKR_OK) {
	st_logf("something went wrong when adding cert!\n");

	/* XXX wack o */;
    }
    hx509_xfree(cert_data.data);
    hx509_xfree(serial_data.data);
    hx509_xfree(issuer_data.data);
    hx509_xfree(subject_data.data);

    return 0;
}

static CK_RV
add_certificate(const char *cert_file,
		const char *pin,
		char *id,
		char *label)
{
    hx509_certs certs;
    hx509_lock lock = NULL;
    int ret, flags = 0;

    struct foo foo;
    foo.id = id;
    foo.label = label;

    if (pin == NULL)
	flags |= HX509_CERTS_UNPROTECT_ALL;

    if (pin) {
	char *str;
	asprintf(&str, "PASS:%s", pin);

	hx509_lock_init(context, &lock);
	hx509_lock_command_string(lock, str);

	memset(str, 0, strlen(str));
	free(str);
    }

    ret = hx509_certs_init(context, cert_file, flags, lock, &certs);
    if (ret) {
	st_logf("failed to open file %s\n", cert_file);
	return CKR_GENERAL_ERROR;
    }

    ret = hx509_certs_iter_f(context, certs, add_cert, &foo);
    hx509_certs_free(&certs);
    if (ret) {
	st_logf("failed adding certs from file %s\n", cert_file);
	return CKR_GENERAL_ERROR;
    }

    return CKR_OK;
}

static void
find_object_final(struct session_state *state)
{
    if (state->find.attributes) {
	CK_ULONG i;

	for (i = 0; i < state->find.num_attributes; i++) {
	    if (state->find.attributes[i].pValue)
		free(state->find.attributes[i].pValue);
	}
	free(state->find.attributes);
	state->find.attributes = NULL;
	state->find.num_attributes = 0;
	state->find.next_object = -1;
    }
}

static void
reset_crypto_state(struct session_state *state)
{
    state->sign_object = -1;
    if (state->sign_mechanism)
	free(state->sign_mechanism);
    state->sign_mechanism = NULL_PTR;
    state->verify_object = -1;
    if (state->verify_mechanism)
	free(state->verify_mechanism);
    state->verify_mechanism = NULL_PTR;
}

static void
close_session(struct session_state *state)
{
    if (state->find.attributes) {
	application_error("application didn't do C_FindObjectsFinal\n");
	find_object_final(state);
    }

    state->session_handle = CK_INVALID_HANDLE;
    soft_token.application = NULL_PTR;
    soft_token.notify = NULL_PTR;
    reset_crypto_state(state);
}

static const char *
has_session(void)
{
    return soft_token.open_sessions > 0 ? "yes" : "no";
}

static CK_RV
read_conf_file(const char *fn, CK_USER_TYPE userType, const char *pin)
{
    char buf[1024], *type, *s, *p;
    FILE *f;
    CK_RV ret = CKR_OK;
    CK_RV failed = CKR_OK;

    if (fn == NULL) {
        st_logf("Can't open configuration file.  No file specified\n");
        return CKR_GENERAL_ERROR;
    }

    f = fopen(fn, "r");
    if (f == NULL) {
	st_logf("can't open configuration file %s\n", fn);
	return CKR_GENERAL_ERROR;
    }
    rk_cloexec_file(f);

    while(fgets(buf, sizeof(buf), f) != NULL) {
	buf[strcspn(buf, "\n")] = '\0';

	st_logf("line: %s\n", buf);

	p = buf;
	while (isspace((unsigned char)*p))
	    p++;
	if (*p == '#')
	    continue;
	while (isspace((unsigned char)*p))
	    p++;

	s = NULL;
	type = strtok_r(p, "\t", &s);
	if (type == NULL)
	    continue;

	if (strcasecmp("certificate", type) == 0) {
	    char *cert, *id, *label;

	    id = strtok_r(NULL, "\t", &s);
	    if (id == NULL) {
		st_logf("no id\n");
		continue;
	    }
	    st_logf("id: %s\n", id);
	    label = strtok_r(NULL, "\t", &s);
	    if (label == NULL) {
		st_logf("no label\n");
		continue;
	    }
	    cert = strtok_r(NULL, "\t", &s);
	    if (cert == NULL) {
		st_logf("no certfiicate store\n");
		continue;
	    }

	    st_logf("adding: %s: %s in file %s\n", id, label, cert);

	    ret = add_certificate(cert, pin, id, label);
	    if (ret)
		failed = ret;
	} else if (strcasecmp("debug", type) == 0) {
	    char *name;

	    name = strtok_r(NULL, "\t", &s);
	    if (name == NULL) {
		st_logf("no filename\n");
		continue;
	    }

	    if (soft_token.logfile)
		fclose(soft_token.logfile);

	    if (strcasecmp(name, "stdout") == 0)
		soft_token.logfile = stdout;
	    else {
		soft_token.logfile = fopen(name, "a");
		if (soft_token.logfile)
		    rk_cloexec_file(soft_token.logfile);
	    }
	    if (soft_token.logfile == NULL)
		st_logf("failed to open file: %s\n", name);

	} else if (strcasecmp("app-fatal", type) == 0) {
	    char *name;

	    name = strtok_r(NULL, "\t", &s);
	    if (name == NULL) {
		st_logf("argument to app-fatal\n");
		continue;
	    }

	    if (strcmp(name, "true") == 0 || strcmp(name, "on") == 0)
		soft_token.flags.app_error_fatal = 1;
	    else if (strcmp(name, "false") == 0 || strcmp(name, "off") == 0)
		soft_token.flags.app_error_fatal = 0;
	    else
		st_logf("unknown app-fatal: %s\n", name);

	} else {
	    st_logf("unknown type: %s\n", type);
	}
    }

    fclose(f);

    return failed;
}

static CK_RV
func_not_supported(void)
{
    st_logf("function not supported\n");
    return CKR_FUNCTION_NOT_SUPPORTED;
}

static char *
get_config_file_for_user(void)
{
    char *fn = NULL;

#ifndef _WIN32
    char *home = NULL;

    if (!issuid()) {
        fn = getenv("SOFTPKCS11RC");
        if (fn)
            fn = strdup(fn);
        home = getenv("HOME");
    }
    if (fn == NULL && home == NULL) {
        struct passwd *pw = getpwuid(getuid());
        if(pw != NULL)
            home = pw->pw_dir;
    }
    if (fn == NULL) {
        if (home)
            asprintf(&fn, "%s/.soft-token.rc", home);
        else
            fn = strdup("/etc/soft-token.rc");
    }
#else  /* Windows */

    char appdatafolder[MAX_PATH];

    fn = getenv("SOFTPKCS11RC");

    /* Retrieve the roaming AppData folder for the current user.  The
       current user is the user account represented by the current
       thread token. */

    if (fn == NULL &&
        SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdatafolder))) {

        asprintf(&fn, "%s\\.soft-token.rc", appdatafolder);
    }

#endif  /* _WIN32 */

    return fn;
}


CK_RV CK_SPEC
C_Initialize(CK_VOID_PTR a)
{
    CK_C_INITIALIZE_ARGS_PTR args = a;
    CK_RV ret;
    size_t i;

    st_logf("Initialize\n");

    INIT_CONTEXT();

    OpenSSL_add_all_algorithms();

    srandom(getpid() ^ (int) time(NULL));

    for (i = 0; i < MAX_NUM_SESSION; i++) {
	soft_token.state[i].session_handle = CK_INVALID_HANDLE;
	soft_token.state[i].find.attributes = NULL;
	soft_token.state[i].find.num_attributes = 0;
	soft_token.state[i].find.next_object = -1;
	reset_crypto_state(&soft_token.state[i]);
    }

    soft_token.flags.hardware_slot = 1;
    soft_token.flags.app_error_fatal = 0;
    soft_token.flags.login_done = 0;

    soft_token.object.objs = NULL;
    soft_token.object.num_objs = 0;

    soft_token.logfile = NULL;
#if 0
    soft_token.logfile = stdout;
#endif
#if 0
    soft_token.logfile = fopen("/tmp/log-pkcs11.txt", "a");
#endif

    if (a != NULL_PTR) {
	st_logf("\tCreateMutex:\t%p\n", args->CreateMutex);
	st_logf("\tDestroyMutext\t%p\n", args->DestroyMutex);
	st_logf("\tLockMutext\t%p\n", args->LockMutex);
	st_logf("\tUnlockMutext\t%p\n", args->UnlockMutex);
	st_logf("\tFlags\t%04x\n", (unsigned int)args->flags);
    }

    soft_token.config_file = get_config_file_for_user();

    /*
     * This operations doesn't return CKR_OK if any of the
     * certificates failes to be unparsed (ie password protected).
     */
    ret = read_conf_file(soft_token.config_file, CKU_USER, NULL);
    if (ret == CKR_OK)
	soft_token.flags.login_done = 1;

    return CKR_OK;
}

CK_RV
C_Finalize(CK_VOID_PTR args)
{
    size_t i;

    INIT_CONTEXT();

    st_logf("Finalize\n");

    for (i = 0; i < MAX_NUM_SESSION; i++) {
	if (soft_token.state[i].session_handle != CK_INVALID_HANDLE) {
	    application_error("application finalized without "
			      "closing session\n");
	    close_session(&soft_token.state[i]);
	}
    }

    return CKR_OK;
}

CK_RV
C_GetInfo(CK_INFO_PTR args)
{
    INIT_CONTEXT();

    st_logf("GetInfo\n");

    memset(args, 17, sizeof(*args));
    args->cryptokiVersion.major = 2;
    args->cryptokiVersion.minor = 10;
    snprintf_fill((char *)args->manufacturerID,
		  sizeof(args->manufacturerID),
		  ' ',
		  "Heimdal hx509 SoftToken");
    snprintf_fill((char *)args->libraryDescription,
		  sizeof(args->libraryDescription), ' ',
		  "Heimdal hx509 SoftToken");
    args->libraryVersion.major = 2;
    args->libraryVersion.minor = 0;

    return CKR_OK;
}

extern CK_FUNCTION_LIST funcs;

CK_RV
C_GetFunctionList(CK_FUNCTION_LIST_PTR_PTR ppFunctionList)
{
    INIT_CONTEXT();

    *ppFunctionList = &funcs;
    return CKR_OK;
}

CK_RV
C_GetSlotList(CK_BBOOL tokenPresent,
	      CK_SLOT_ID_PTR pSlotList,
	      CK_ULONG_PTR   pulCount)
{
    INIT_CONTEXT();
    st_logf("GetSlotList: %s\n",
	    tokenPresent ? "tokenPresent" : "token not Present");
    if (pSlotList)
	pSlotList[0] = 1;
    *pulCount = 1;
    return CKR_OK;
}

CK_RV
C_GetSlotInfo(CK_SLOT_ID slotID,
	      CK_SLOT_INFO_PTR pInfo)
{
    INIT_CONTEXT();
    st_logf("GetSlotInfo: slot: %d : %s\n", (int)slotID, has_session());

    memset(pInfo, 18, sizeof(*pInfo));

    if (slotID != 1)
	return CKR_ARGUMENTS_BAD;

    snprintf_fill((char *)pInfo->slotDescription,
		  sizeof(pInfo->slotDescription),
		  ' ',
		  "Heimdal hx509 SoftToken (slot)");
    snprintf_fill((char *)pInfo->manufacturerID,
		  sizeof(pInfo->manufacturerID),
		  ' ',
		  "Heimdal hx509 SoftToken (slot)");
    pInfo->flags = CKF_TOKEN_PRESENT;
    if (soft_token.flags.hardware_slot)
	pInfo->flags |= CKF_HW_SLOT;
    pInfo->hardwareVersion.major = 1;
    pInfo->hardwareVersion.minor = 0;
    pInfo->firmwareVersion.major = 1;
    pInfo->firmwareVersion.minor = 0;

    return CKR_OK;
}

CK_RV
C_GetTokenInfo(CK_SLOT_ID slotID,
	       CK_TOKEN_INFO_PTR pInfo)
{
    INIT_CONTEXT();
    st_logf("GetTokenInfo: %s\n", has_session());

    memset(pInfo, 19, sizeof(*pInfo));

    snprintf_fill((char *)pInfo->label,
		  sizeof(pInfo->label),
		  ' ',
		  "Heimdal hx509 SoftToken (token)");
    snprintf_fill((char *)pInfo->manufacturerID,
		  sizeof(pInfo->manufacturerID),
		  ' ',
		  "Heimdal hx509 SoftToken (token)");
    snprintf_fill((char *)pInfo->model,
		  sizeof(pInfo->model),
		  ' ',
		  "Heimdal hx509 SoftToken (token)");
    snprintf_fill((char *)pInfo->serialNumber,
		  sizeof(pInfo->serialNumber),
		  ' ',
		  "4711");
    pInfo->flags =
	CKF_TOKEN_INITIALIZED |
	CKF_USER_PIN_INITIALIZED;

    if (soft_token.flags.login_done == 0)
	pInfo->flags |= CKF_LOGIN_REQUIRED;

    /* CFK_RNG |
       CKF_RESTORE_KEY_NOT_NEEDED |
    */
    pInfo->ulMaxSessionCount = MAX_NUM_SESSION;
    pInfo->ulSessionCount = soft_token.open_sessions;
    pInfo->ulMaxRwSessionCount = MAX_NUM_SESSION;
    pInfo->ulRwSessionCount = soft_token.open_sessions;
    pInfo->ulMaxPinLen = 1024;
    pInfo->ulMinPinLen = 0;
    pInfo->ulTotalPublicMemory = 4711;
    pInfo->ulFreePublicMemory = 4712;
    pInfo->ulTotalPrivateMemory = 4713;
    pInfo->ulFreePrivateMemory = 4714;
    pInfo->hardwareVersion.major = 2;
    pInfo->hardwareVersion.minor = 0;
    pInfo->firmwareVersion.major = 2;
    pInfo->firmwareVersion.minor = 0;

    return CKR_OK;
}

CK_RV
C_GetMechanismList(CK_SLOT_ID slotID,
		   CK_MECHANISM_TYPE_PTR pMechanismList,
		   CK_ULONG_PTR pulCount)
{
    INIT_CONTEXT();
    st_logf("GetMechanismList\n");

    *pulCount = 1;
    if (pMechanismList == NULL_PTR)
	return CKR_OK;
    pMechanismList[1] = CKM_RSA_PKCS;

    return CKR_OK;
}

CK_RV
C_GetMechanismInfo(CK_SLOT_ID slotID,
		   CK_MECHANISM_TYPE type,
		   CK_MECHANISM_INFO_PTR pInfo)
{
    INIT_CONTEXT();
    st_logf("GetMechanismInfo: slot %d type: %d\n",
	    (int)slotID, (int)type);
    memset(pInfo, 0, sizeof(*pInfo));

    return CKR_OK;
}

CK_RV
C_InitToken(CK_SLOT_ID slotID,
	    CK_UTF8CHAR_PTR pPin,
	    CK_ULONG ulPinLen,
	    CK_UTF8CHAR_PTR pLabel)
{
    INIT_CONTEXT();
    st_logf("InitToken: slot %d\n", (int)slotID);
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV
C_OpenSession(CK_SLOT_ID slotID,
	      CK_FLAGS flags,
	      CK_VOID_PTR pApplication,
	      CK_NOTIFY Notify,
	      CK_SESSION_HANDLE_PTR phSession)
{
    size_t i;
    INIT_CONTEXT();
    st_logf("OpenSession: slot: %d\n", (int)slotID);

    if (soft_token.open_sessions == MAX_NUM_SESSION)
	return CKR_SESSION_COUNT;

    soft_token.application = pApplication;
    soft_token.notify = Notify;

    for (i = 0; i < MAX_NUM_SESSION; i++)
	if (soft_token.state[i].session_handle == CK_INVALID_HANDLE)
	    break;
    if (i == MAX_NUM_SESSION)
	abort();

    soft_token.open_sessions++;

    soft_token.state[i].session_handle =
	(CK_SESSION_HANDLE)(random() & 0xfffff);
    *phSession = soft_token.state[i].session_handle;

    return CKR_OK;
}

CK_RV
C_CloseSession(CK_SESSION_HANDLE hSession)
{
    struct session_state *state;
    INIT_CONTEXT();
    st_logf("CloseSession\n");

    if (verify_session_handle(hSession, &state) != CKR_OK)
	application_error("closed session not open");
    else
	close_session(state);

    return CKR_OK;
}

CK_RV
C_CloseAllSessions(CK_SLOT_ID slotID)
{
    size_t i;
    INIT_CONTEXT();

    st_logf("CloseAllSessions\n");

    for (i = 0; i < MAX_NUM_SESSION; i++)
	if (soft_token.state[i].session_handle != CK_INVALID_HANDLE)
	    close_session(&soft_token.state[i]);

    return CKR_OK;
}

CK_RV
C_GetSessionInfo(CK_SESSION_HANDLE hSession,
		 CK_SESSION_INFO_PTR pInfo)
{
    st_logf("GetSessionInfo\n");
    INIT_CONTEXT();

    VERIFY_SESSION_HANDLE(hSession, NULL);

    memset(pInfo, 20, sizeof(*pInfo));

    pInfo->slotID = 1;
    if (soft_token.flags.login_done)
	pInfo->state = CKS_RO_USER_FUNCTIONS;
    else
	pInfo->state = CKS_RO_PUBLIC_SESSION;
    pInfo->flags = CKF_SERIAL_SESSION;
    pInfo->ulDeviceError = 0;

    return CKR_OK;
}

CK_RV
C_Login(CK_SESSION_HANDLE hSession,
	CK_USER_TYPE userType,
	CK_UTF8CHAR_PTR pPin,
	CK_ULONG ulPinLen)
{
    char *pin = NULL;
    CK_RV ret;
    INIT_CONTEXT();

    st_logf("Login\n");

    VERIFY_SESSION_HANDLE(hSession, NULL);

    if (pPin != NULL_PTR) {
	asprintf(&pin, "%.*s", (int)ulPinLen, pPin);
	st_logf("type: %d password: %s\n", (int)userType, pin);
    }

    /*
     * Login
     */

    ret = read_conf_file(soft_token.config_file, userType, pin);
    if (ret == CKR_OK)
	soft_token.flags.login_done = 1;

    free(pin);

    return soft_token.flags.login_done ? CKR_OK : CKR_PIN_INCORRECT;
}

CK_RV
C_Logout(CK_SESSION_HANDLE hSession)
{
    st_logf("Logout\n");
    INIT_CONTEXT();

    VERIFY_SESSION_HANDLE(hSession, NULL);
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV
C_GetObjectSize(CK_SESSION_HANDLE hSession,
		CK_OBJECT_HANDLE hObject,
		CK_ULONG_PTR pulSize)
{
    st_logf("GetObjectSize\n");
    INIT_CONTEXT();

    VERIFY_SESSION_HANDLE(hSession, NULL);
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV
C_GetAttributeValue(CK_SESSION_HANDLE hSession,
		    CK_OBJECT_HANDLE hObject,
		    CK_ATTRIBUTE_PTR pTemplate,
		    CK_ULONG ulCount)
{
    struct session_state *state;
    struct st_object *obj;
    CK_ULONG i;
    CK_RV ret;
    int j;

    INIT_CONTEXT();

    st_logf("GetAttributeValue: %lx\n",
	    (unsigned long)HANDLE_OBJECT_ID(hObject));
    VERIFY_SESSION_HANDLE(hSession, &state);

    if ((ret = object_handle_to_object(hObject, &obj)) != CKR_OK) {
	st_logf("object not found: %lx\n",
		(unsigned long)HANDLE_OBJECT_ID(hObject));
	return ret;
    }

    for (i = 0; i < ulCount; i++) {
	st_logf("	getting 0x%08lx\n", (unsigned long)pTemplate[i].type);
	for (j = 0; j < obj->num_attributes; j++) {
	    if (obj->attrs[j].secret) {
		pTemplate[i].ulValueLen = (CK_ULONG)-1;
		break;
	    }
	    if (pTemplate[i].type == obj->attrs[j].attribute.type) {
		if (pTemplate[i].pValue != NULL_PTR && obj->attrs[j].secret == 0) {
		    if (pTemplate[i].ulValueLen >= obj->attrs[j].attribute.ulValueLen)
			memcpy(pTemplate[i].pValue, obj->attrs[j].attribute.pValue,
			       obj->attrs[j].attribute.ulValueLen);
		}
		pTemplate[i].ulValueLen = obj->attrs[j].attribute.ulValueLen;
		break;
	    }
	}
	if (j == obj->num_attributes) {
	    st_logf("key type: 0x%08lx not found\n", (unsigned long)pTemplate[i].type);
	    pTemplate[i].ulValueLen = (CK_ULONG)-1;
	}

    }
    return CKR_OK;
}

CK_RV
C_FindObjectsInit(CK_SESSION_HANDLE hSession,
		  CK_ATTRIBUTE_PTR pTemplate,
		  CK_ULONG ulCount)
{
    struct session_state *state;

    st_logf("FindObjectsInit\n");

    INIT_CONTEXT();

    VERIFY_SESSION_HANDLE(hSession, &state);

    if (state->find.next_object != -1) {
	application_error("application didn't do C_FindObjectsFinal\n");
	find_object_final(state);
    }
    if (ulCount) {
	CK_ULONG i;

	print_attributes(pTemplate, ulCount);

	state->find.attributes =
	    calloc(1, ulCount * sizeof(state->find.attributes[0]));
	if (state->find.attributes == NULL)
	    return CKR_DEVICE_MEMORY;
	for (i = 0; i < ulCount; i++) {
	    state->find.attributes[i].pValue =
		malloc(pTemplate[i].ulValueLen);
	    if (state->find.attributes[i].pValue == NULL) {
		find_object_final(state);
		return CKR_DEVICE_MEMORY;
	    }
	    memcpy(state->find.attributes[i].pValue,
		   pTemplate[i].pValue, pTemplate[i].ulValueLen);
	    state->find.attributes[i].type = pTemplate[i].type;
	    state->find.attributes[i].ulValueLen = pTemplate[i].ulValueLen;
	}
	state->find.num_attributes = ulCount;
	state->find.next_object = 0;
    } else {
	st_logf("find all objects\n");
	state->find.attributes = NULL;
	state->find.num_attributes = 0;
	state->find.next_object = 0;
    }

    return CKR_OK;
}

CK_RV
C_FindObjects(CK_SESSION_HANDLE hSession,
	      CK_OBJECT_HANDLE_PTR phObject,
	      CK_ULONG ulMaxObjectCount,
	      CK_ULONG_PTR pulObjectCount)
{
    struct session_state *state;
    int i;

    INIT_CONTEXT();

    st_logf("FindObjects\n");

    VERIFY_SESSION_HANDLE(hSession, &state);

    if (state->find.next_object == -1) {
	application_error("application didn't do C_FindObjectsInit\n");
	return CKR_ARGUMENTS_BAD;
    }
    if (ulMaxObjectCount == 0) {
	application_error("application asked for 0 objects\n");
	return CKR_ARGUMENTS_BAD;
    }
    *pulObjectCount = 0;
    for (i = state->find.next_object; i < soft_token.object.num_objs; i++) {
	st_logf("FindObjects: %d\n", i);
	state->find.next_object = i + 1;
	if (attributes_match(soft_token.object.objs[i],
			     state->find.attributes,
			     state->find.num_attributes)) {
	    *phObject++ = soft_token.object.objs[i]->object_handle;
	    ulMaxObjectCount--;
	    (*pulObjectCount)++;
	    if (ulMaxObjectCount == 0)
		break;
	}
    }
    return CKR_OK;
}

CK_RV
C_FindObjectsFinal(CK_SESSION_HANDLE hSession)
{
    struct session_state *state;

    INIT_CONTEXT();

    st_logf("FindObjectsFinal\n");
    VERIFY_SESSION_HANDLE(hSession, &state);
    find_object_final(state);
    return CKR_OK;
}

static CK_RV
commonInit(CK_ATTRIBUTE *attr_match, int attr_match_len,
	   const CK_MECHANISM_TYPE *mechs, int mechs_len,
	   const CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey,
	   struct st_object **o)
{
    CK_RV ret;
    int i;

    *o = NULL;
    if ((ret = object_handle_to_object(hKey, o)) != CKR_OK)
	return ret;

    ret = attributes_match(*o, attr_match, attr_match_len);
    if (!ret) {
	application_error("called commonInit on key that doesn't "
			  "support required attr");
	return CKR_ARGUMENTS_BAD;
    }

    for (i = 0; i < mechs_len; i++)
	if (mechs[i] == pMechanism->mechanism)
	    break;
    if (i == mechs_len) {
	application_error("called mech (%08lx) not supported\n",
			  pMechanism->mechanism);
	return CKR_ARGUMENTS_BAD;
    }
    return CKR_OK;
}


static CK_RV
dup_mechanism(CK_MECHANISM_PTR *dp, const CK_MECHANISM_PTR pMechanism)
{
    CK_MECHANISM_PTR p;

    p = malloc(sizeof(*p));
    if (p == NULL)
	return CKR_DEVICE_MEMORY;

    if (*dp)
	free(*dp);
    *dp = p;
    memcpy(p, pMechanism, sizeof(*p));

    return CKR_OK;
}

CK_RV
C_DigestInit(CK_SESSION_HANDLE hSession,
	     CK_MECHANISM_PTR pMechanism)
{
    st_logf("DigestInit\n");
    INIT_CONTEXT();
    VERIFY_SESSION_HANDLE(hSession, NULL);
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV
C_SignInit(CK_SESSION_HANDLE hSession,
	   CK_MECHANISM_PTR pMechanism,
	   CK_OBJECT_HANDLE hKey)
{
    struct session_state *state;
    CK_MECHANISM_TYPE mechs[] = { CKM_RSA_PKCS };
    CK_BBOOL bool_true = CK_TRUE;
    CK_ATTRIBUTE attr[] = {
	{ CKA_SIGN, &bool_true, sizeof(bool_true) }
    };
    struct st_object *o;
    CK_RV ret;

    INIT_CONTEXT();
    st_logf("SignInit\n");
    VERIFY_SESSION_HANDLE(hSession, &state);

    ret = commonInit(attr, sizeof(attr)/sizeof(attr[0]),
		     mechs, sizeof(mechs)/sizeof(mechs[0]),
		     pMechanism, hKey, &o);
    if (ret)
	return ret;

    ret = dup_mechanism(&state->sign_mechanism, pMechanism);
    if (ret == CKR_OK)
	state->sign_object = OBJECT_ID(o);

    return CKR_OK;
}

CK_RV
C_Sign(CK_SESSION_HANDLE hSession,
       CK_BYTE_PTR pData,
       CK_ULONG ulDataLen,
       CK_BYTE_PTR pSignature,
       CK_ULONG_PTR pulSignatureLen)
{
    struct session_state *state;
    struct st_object *o;
    CK_RV ret;
    int hret;
    const AlgorithmIdentifier *alg;
    heim_octet_string sig, data;

    INIT_CONTEXT();
    st_logf("Sign\n");
    VERIFY_SESSION_HANDLE(hSession, &state);

    sig.data = NULL;
    sig.length = 0;

    if (state->sign_object == -1)
	return CKR_ARGUMENTS_BAD;

    if (pulSignatureLen == NULL) {
	st_logf("signature len NULL\n");
	ret = CKR_ARGUMENTS_BAD;
	goto out;
    }

    if (pData == NULL_PTR) {
	st_logf("data NULL\n");
	ret = CKR_ARGUMENTS_BAD;
	goto out;
    }

    o = soft_token.object.objs[state->sign_object];

    if (hx509_cert_have_private_key(o->cert) == 0) {
	st_logf("private key NULL\n");
	return CKR_ARGUMENTS_BAD;
    }

    switch(state->sign_mechanism->mechanism) {
    case CKM_RSA_PKCS:
	alg = hx509_signature_rsa_pkcs1_x509();
	break;
    default:
	ret = CKR_FUNCTION_NOT_SUPPORTED;
	goto out;
    }

    data.data = pData;
    data.length = ulDataLen;

    hret = _hx509_create_signature(context,
				   _hx509_cert_private_key(o->cert),
				   alg,
				   &data,
				   NULL,
				   &sig);
    if (hret) {
	ret = CKR_DEVICE_ERROR;
	goto out;
    }
    *pulSignatureLen = sig.length;

    if (pSignature != NULL_PTR)
	memcpy(pSignature, sig.data, sig.length);

    ret = CKR_OK;
 out:
    if (sig.data) {
	memset(sig.data, 0, sig.length);
	der_free_octet_string(&sig);
    }
    return ret;
}

CK_RV
C_SignUpdate(CK_SESSION_HANDLE hSession,
	     CK_BYTE_PTR pPart,
	     CK_ULONG ulPartLen)
{
    INIT_CONTEXT();
    st_logf("SignUpdate\n");
    VERIFY_SESSION_HANDLE(hSession, NULL);
    return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_RV
C_SignFinal(CK_SESSION_HANDLE hSession,
	    CK_BYTE_PTR pSignature,
	    CK_ULONG_PTR pulSignatureLen)
{
    INIT_CONTEXT();
    st_logf("SignUpdate\n");
    VERIFY_SESSION_HANDLE(hSession, NULL);
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV
C_VerifyInit(CK_SESSION_HANDLE hSession,
	     CK_MECHANISM_PTR pMechanism,
	     CK_OBJECT_HANDLE hKey)
{
    struct session_state *state;
    CK_MECHANISM_TYPE mechs[] = { CKM_RSA_PKCS };
    CK_BBOOL bool_true = CK_TRUE;
    CK_ATTRIBUTE attr[] = {
	{ CKA_VERIFY, &bool_true, sizeof(bool_true) }
    };
    struct st_object *o;
    CK_RV ret;

    INIT_CONTEXT();
    st_logf("VerifyInit\n");
    VERIFY_SESSION_HANDLE(hSession, &state);

    ret = commonInit(attr, sizeof(attr)/sizeof(attr[0]),
		     mechs, sizeof(mechs)/sizeof(mechs[0]),
		     pMechanism, hKey, &o);
    if (ret)
	return ret;

    ret = dup_mechanism(&state->verify_mechanism, pMechanism);
    if (ret == CKR_OK)
	state->verify_object = OBJECT_ID(o);

    return ret;
}

CK_RV
C_Verify(CK_SESSION_HANDLE hSession,
	 CK_BYTE_PTR pData,
	 CK_ULONG ulDataLen,
	 CK_BYTE_PTR pSignature,
	 CK_ULONG ulSignatureLen)
{
    struct session_state *state;
    struct st_object *o;
    const AlgorithmIdentifier *alg;
    CK_RV ret;
    int hret;
    heim_octet_string data, sig;

    INIT_CONTEXT();
    st_logf("Verify\n");
    VERIFY_SESSION_HANDLE(hSession, &state);

    if (state->verify_object == -1)
	return CKR_ARGUMENTS_BAD;

    o = soft_token.object.objs[state->verify_object];

    switch(state->verify_mechanism->mechanism) {
    case CKM_RSA_PKCS:
	alg = hx509_signature_rsa_pkcs1_x509();
	break;
    default:
	ret = CKR_FUNCTION_NOT_SUPPORTED;
	goto out;
    }

    sig.data = pData;
    sig.length = ulDataLen;
    data.data = pSignature;
    data.length = ulSignatureLen;

    hret = _hx509_verify_signature(context,
				   o->cert,
				   alg,
				   &data,
				   &sig);
    if (hret) {
	ret = CKR_GENERAL_ERROR;
	goto out;
    }
    ret = CKR_OK;

 out:
    return ret;
}


CK_RV
C_VerifyUpdate(CK_SESSION_HANDLE hSession,
	       CK_BYTE_PTR pPart,
	       CK_ULONG ulPartLen)
{
    INIT_CONTEXT();
    st_logf("VerifyUpdate\n");
    VERIFY_SESSION_HANDLE(hSession, NULL);
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV
C_VerifyFinal(CK_SESSION_HANDLE hSession,
	      CK_BYTE_PTR pSignature,
	      CK_ULONG ulSignatureLen)
{
    INIT_CONTEXT();
    st_logf("VerifyFinal\n");
    VERIFY_SESSION_HANDLE(hSession, NULL);
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV
C_GenerateRandom(CK_SESSION_HANDLE hSession,
		 CK_BYTE_PTR RandomData,
		 CK_ULONG ulRandomLen)
{
    INIT_CONTEXT();
    st_logf("GenerateRandom\n");
    VERIFY_SESSION_HANDLE(hSession, NULL);
    return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_FUNCTION_LIST funcs = {
    { 2, 11 },
    C_Initialize,
    C_Finalize,
    C_GetInfo,
    C_GetFunctionList,
    C_GetSlotList,
    C_GetSlotInfo,
    C_GetTokenInfo,
    C_GetMechanismList,
    C_GetMechanismInfo,
    C_InitToken,
    (void *)func_not_supported, /* C_InitPIN */
    (void *)func_not_supported, /* C_SetPIN */
    C_OpenSession,
    C_CloseSession,
    C_CloseAllSessions,
    C_GetSessionInfo,
    (void *)func_not_supported, /* C_GetOperationState */
    (void *)func_not_supported, /* C_SetOperationState */
    C_Login,
    C_Logout,
    (void *)func_not_supported, /* C_CreateObject */
    (void *)func_not_supported, /* C_CopyObject */
    (void *)func_not_supported, /* C_DestroyObject */
    (void *)func_not_supported, /* C_GetObjectSize */
    C_GetAttributeValue,
    (void *)func_not_supported, /* C_SetAttributeValue */
    C_FindObjectsInit,
    C_FindObjects,
    C_FindObjectsFinal,
    (void *)func_not_supported, /* C_EncryptInit, */
    (void *)func_not_supported, /* C_Encrypt, */
    (void *)func_not_supported, /* C_EncryptUpdate, */
    (void *)func_not_supported, /* C_EncryptFinal, */
    (void *)func_not_supported, /* C_DecryptInit, */
    (void *)func_not_supported, /* C_Decrypt, */
    (void *)func_not_supported, /* C_DecryptUpdate, */
    (void *)func_not_supported, /* C_DecryptFinal, */
    C_DigestInit,
    (void *)func_not_supported, /* C_Digest */
    (void *)func_not_supported, /* C_DigestUpdate */
    (void *)func_not_supported, /* C_DigestKey */
    (void *)func_not_supported, /* C_DigestFinal */
    C_SignInit,
    C_Sign,
    C_SignUpdate,
    C_SignFinal,
    (void *)func_not_supported, /* C_SignRecoverInit */
    (void *)func_not_supported, /* C_SignRecover */
    C_VerifyInit,
    C_Verify,
    C_VerifyUpdate,
    C_VerifyFinal,
    (void *)func_not_supported, /* C_VerifyRecoverInit */
    (void *)func_not_supported, /* C_VerifyRecover */
    (void *)func_not_supported, /* C_DigestEncryptUpdate */
    (void *)func_not_supported, /* C_DecryptDigestUpdate */
    (void *)func_not_supported, /* C_SignEncryptUpdate */
    (void *)func_not_supported, /* C_DecryptVerifyUpdate */
    (void *)func_not_supported, /* C_GenerateKey */
    (void *)func_not_supported, /* C_GenerateKeyPair */
    (void *)func_not_supported, /* C_WrapKey */
    (void *)func_not_supported, /* C_UnwrapKey */
    (void *)func_not_supported, /* C_DeriveKey */
    (void *)func_not_supported, /* C_SeedRandom */
    C_GenerateRandom,
    (void *)func_not_supported, /* C_GetFunctionStatus */
    (void *)func_not_supported, /* C_CancelFunction */
    (void *)func_not_supported  /* C_WaitForSlotEvent */
};
