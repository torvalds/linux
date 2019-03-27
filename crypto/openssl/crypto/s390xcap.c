/*
 * Copyright 2010-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>
#include "internal/cryptlib.h"
#include "s390x_arch.h"

static sigjmp_buf ill_jmp;
static void ill_handler(int sig)
{
    siglongjmp(ill_jmp, sig);
}

void OPENSSL_s390x_facilities(void);
void OPENSSL_vx_probe(void);

struct OPENSSL_s390xcap_st OPENSSL_s390xcap_P;

void OPENSSL_cpuid_setup(void)
{
    sigset_t oset;
    struct sigaction ill_act, oact;

    if (OPENSSL_s390xcap_P.stfle[0])
        return;

    /* set a bit that will not be tested later */
    OPENSSL_s390xcap_P.stfle[0] |= S390X_CAPBIT(0);

    memset(&ill_act, 0, sizeof(ill_act));
    ill_act.sa_handler = ill_handler;
    sigfillset(&ill_act.sa_mask);
    sigdelset(&ill_act.sa_mask, SIGILL);
    sigdelset(&ill_act.sa_mask, SIGFPE);
    sigdelset(&ill_act.sa_mask, SIGTRAP);
    sigprocmask(SIG_SETMASK, &ill_act.sa_mask, &oset);
    sigaction(SIGILL, &ill_act, &oact);
    sigaction(SIGFPE, &ill_act, &oact);

    /* protection against missing store-facility-list-extended */
    if (sigsetjmp(ill_jmp, 1) == 0)
        OPENSSL_s390x_facilities();

    /* protection against disabled vector facility */
    if ((OPENSSL_s390xcap_P.stfle[2] & S390X_CAPBIT(S390X_VX))
        && (sigsetjmp(ill_jmp, 1) == 0)) {
        OPENSSL_vx_probe();
    } else {
        OPENSSL_s390xcap_P.stfle[2] &= ~(S390X_CAPBIT(S390X_VX)
                                         | S390X_CAPBIT(S390X_VXD)
                                         | S390X_CAPBIT(S390X_VXE));
    }

    sigaction(SIGFPE, &oact, NULL);
    sigaction(SIGILL, &oact, NULL);
    sigprocmask(SIG_SETMASK, &oset, NULL);
}
