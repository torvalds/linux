/*
 * Copyright 2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "ui_locl.h"

static const UI_METHOD ui_null = {
    "OpenSSL NULL UI",
    NULL,                        /* opener */
    NULL,                        /* writer */
    NULL,                        /* flusher */
    NULL,                        /* reader */
    NULL,                        /* closer */
    NULL
};

/* The method with all the built-in thingies */
const UI_METHOD *UI_null(void)
{
    return &ui_null;
}
