#include "mech_locl.h"
#include "heim_threads.h"

struct mg_thread_ctx {
    gss_OID mech;
    OM_uint32 maj_stat;
    OM_uint32 min_stat;
    gss_buffer_desc maj_error;
    gss_buffer_desc min_error;
};

static HEIMDAL_MUTEX context_mutex = HEIMDAL_MUTEX_INITIALIZER;
static int created_key;
static HEIMDAL_thread_key context_key;


static void
destroy_context(void *ptr)
{
    struct mg_thread_ctx *mg = ptr;
    OM_uint32 junk;

    if (mg == NULL)
	return;

    gss_release_buffer(&junk, &mg->maj_error);
    gss_release_buffer(&junk, &mg->min_error);
    free(mg);
}


static struct mg_thread_ctx *
_gss_mechglue_thread(void)
{
    struct mg_thread_ctx *ctx;
    int ret = 0;

    HEIMDAL_MUTEX_lock(&context_mutex);

    if (!created_key) {
	HEIMDAL_key_create(&context_key, destroy_context, ret);
	if (ret) {
	    HEIMDAL_MUTEX_unlock(&context_mutex);
	    return NULL;
	}
	created_key = 1;
    }
    HEIMDAL_MUTEX_unlock(&context_mutex);

    ctx = HEIMDAL_getspecific(context_key);
    if (ctx == NULL) {

	ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL)
	    return NULL;
	HEIMDAL_setspecific(context_key, ctx, ret);
	if (ret) {
	    free(ctx);
	    return NULL;
	}
    }
    return ctx;
}

OM_uint32
_gss_mg_get_error(const gss_OID mech, OM_uint32 type,
		  OM_uint32 value, gss_buffer_t string)
{
    struct mg_thread_ctx *mg;

    mg = _gss_mechglue_thread();
    if (mg == NULL)
	return GSS_S_BAD_STATUS;

#if 0
    /*
     * We cant check the mech here since a pseudo-mech might have
     * called an lower layer and then the mech info is all broken
     */
    if (mech != NULL && gss_oid_equal(mg->mech, mech) == 0)
	return GSS_S_BAD_STATUS;
#endif

    switch (type) {
    case GSS_C_GSS_CODE: {
	if (value != mg->maj_stat || mg->maj_error.length == 0)
	    break;
	string->value = malloc(mg->maj_error.length + 1);
	string->length = mg->maj_error.length;
	memcpy(string->value, mg->maj_error.value, mg->maj_error.length);
        ((char *) string->value)[string->length] = '\0';
	return GSS_S_COMPLETE;
    }
    case GSS_C_MECH_CODE: {
	if (value != mg->min_stat || mg->min_error.length == 0)
	    break;
	string->value = malloc(mg->min_error.length + 1);
	string->length = mg->min_error.length;
	memcpy(string->value, mg->min_error.value, mg->min_error.length);
        ((char *) string->value)[string->length] = '\0';
	return GSS_S_COMPLETE;
    }
    }
    string->value = NULL;
    string->length = 0;
    return GSS_S_BAD_STATUS;
}

void
_gss_mg_error(gssapi_mech_interface m, OM_uint32 maj, OM_uint32 min)
{
    OM_uint32 major_status, minor_status;
    OM_uint32 message_content;
    struct mg_thread_ctx *mg;

    /*
     * Mechs without gss_display_status() does
     * gss_mg_collect_error() by themself.
     */
    if (m->gm_display_status == NULL)
	return ;

    mg = _gss_mechglue_thread();
    if (mg == NULL)
	return;

    gss_release_buffer(&minor_status, &mg->maj_error);
    gss_release_buffer(&minor_status, &mg->min_error);

    mg->mech = &m->gm_mech_oid;
    mg->maj_stat = maj;
    mg->min_stat = min;

    major_status = m->gm_display_status(&minor_status,
					maj,
					GSS_C_GSS_CODE,
					&m->gm_mech_oid,
					&message_content,
					&mg->maj_error);
    if (GSS_ERROR(major_status)) {
	mg->maj_error.value = NULL;
	mg->maj_error.length = 0;
    }
    major_status = m->gm_display_status(&minor_status,
					min,
					GSS_C_MECH_CODE,
					&m->gm_mech_oid,
					&message_content,
					&mg->min_error);
    if (GSS_ERROR(major_status)) {
	mg->min_error.value = NULL;
	mg->min_error.length = 0;
    }
}

void
gss_mg_collect_error(gss_OID mech, OM_uint32 maj, OM_uint32 min)
{
    gssapi_mech_interface m = __gss_get_mechanism(mech);
    if (m == NULL)
	return;
    _gss_mg_error(m, maj, min);
}
