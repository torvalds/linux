/*
 * Copyright 2002-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <string.h>
#include "internal/thread_once.h"
#include "ui_locl.h"

#ifndef BUFSIZ
#define BUFSIZ 256
#endif

int UI_UTIL_read_pw_string(char *buf, int length, const char *prompt,
                           int verify)
{
    char buff[BUFSIZ];
    int ret;

    ret =
        UI_UTIL_read_pw(buf, buff, (length > BUFSIZ) ? BUFSIZ : length,
                        prompt, verify);
    OPENSSL_cleanse(buff, BUFSIZ);
    return ret;
}

int UI_UTIL_read_pw(char *buf, char *buff, int size, const char *prompt,
                    int verify)
{
    int ok = 0;
    UI *ui;

    if (size < 1)
        return -1;

    ui = UI_new();
    if (ui != NULL) {
        ok = UI_add_input_string(ui, prompt, 0, buf, 0, size - 1);
        if (ok >= 0 && verify)
            ok = UI_add_verify_string(ui, prompt, 0, buff, 0, size - 1, buf);
        if (ok >= 0)
            ok = UI_process(ui);
        UI_free(ui);
    }
    if (ok > 0)
        ok = 0;
    return ok;
}

/*
 * Wrapper around pem_password_cb, a method to help older APIs use newer
 * ones.
 */
struct pem_password_cb_data {
    pem_password_cb *cb;
    int rwflag;
};

static void ui_new_method_data(void *parent, void *ptr, CRYPTO_EX_DATA *ad,
                               int idx, long argl, void *argp)
{
    /*
     * Do nothing, the data is allocated externally and assigned later with
     * CRYPTO_set_ex_data()
     */
}

static int ui_dup_method_data(CRYPTO_EX_DATA *to, const CRYPTO_EX_DATA *from,
                              void *from_d, int idx, long argl, void *argp)
{
    void **pptr = (void **)from_d;
    if (*pptr != NULL)
        *pptr = OPENSSL_memdup(*pptr, sizeof(struct pem_password_cb_data));
    return 1;
}

static void ui_free_method_data(void *parent, void *ptr, CRYPTO_EX_DATA *ad,
                                int idx, long argl, void *argp)
{
    OPENSSL_free(ptr);
}

static CRYPTO_ONCE get_index_once = CRYPTO_ONCE_STATIC_INIT;
static int ui_method_data_index = -1;
DEFINE_RUN_ONCE_STATIC(ui_method_data_index_init)
{
    ui_method_data_index = CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_UI_METHOD,
                                                   0, NULL, ui_new_method_data,
                                                   ui_dup_method_data,
                                                   ui_free_method_data);
    return 1;
}

static int ui_open(UI *ui)
{
    return 1;
}
static int ui_read(UI *ui, UI_STRING *uis)
{
    switch (UI_get_string_type(uis)) {
    case UIT_PROMPT:
        {
            char result[PEM_BUFSIZE + 1];
            const struct pem_password_cb_data *data =
                UI_method_get_ex_data(UI_get_method(ui), ui_method_data_index);
            int maxsize = UI_get_result_maxsize(uis);
            int len = data->cb(result,
                               maxsize > PEM_BUFSIZE ? PEM_BUFSIZE : maxsize,
                               data->rwflag, UI_get0_user_data(ui));

            if (len >= 0)
                result[len] = '\0';
            if (len <= 0)
                return len;
            if (UI_set_result_ex(ui, uis, result, len) >= 0)
                return 1;
            return 0;
        }
    case UIT_VERIFY:
    case UIT_NONE:
    case UIT_BOOLEAN:
    case UIT_INFO:
    case UIT_ERROR:
        break;
    }
    return 1;
}
static int ui_write(UI *ui, UI_STRING *uis)
{
    return 1;
}
static int ui_close(UI *ui)
{
    return 1;
}

UI_METHOD *UI_UTIL_wrap_read_pem_callback(pem_password_cb *cb, int rwflag)
{
    struct pem_password_cb_data *data = NULL;
    UI_METHOD *ui_method = NULL;

    if ((data = OPENSSL_zalloc(sizeof(*data))) == NULL
        || (ui_method = UI_create_method("PEM password callback wrapper")) == NULL
        || UI_method_set_opener(ui_method, ui_open) < 0
        || UI_method_set_reader(ui_method, ui_read) < 0
        || UI_method_set_writer(ui_method, ui_write) < 0
        || UI_method_set_closer(ui_method, ui_close) < 0
        || !RUN_ONCE(&get_index_once, ui_method_data_index_init)
        || UI_method_set_ex_data(ui_method, ui_method_data_index, data) < 0) {
        UI_destroy_method(ui_method);
        OPENSSL_free(data);
        return NULL;
    }
    data->rwflag = rwflag;
    data->cb = cb;

    return ui_method;
}
