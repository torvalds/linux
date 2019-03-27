/*
 * Copyright 2009-2019 The OpenSSL Project Authors. All Rights Reserved.
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
#include <unistd.h>
#if defined(__linux) || defined(_AIX)
# include <sys/utsname.h>
#endif
#if defined(_AIX53)     /* defined even on post-5.3 */
# include <sys/systemcfg.h>
# if !defined(__power_set)
#  define __power_set(a) (_system_configuration.implementation & (a))
# endif
#endif
#if defined(__APPLE__) && defined(__MACH__)
# include <sys/types.h>
# include <sys/sysctl.h>
#endif
#include <openssl/crypto.h>
#include <openssl/bn.h>
#include <internal/cryptlib.h>
#include <internal/chacha.h>
#include "bn/bn_lcl.h"

#include "ppc_arch.h"

unsigned int OPENSSL_ppccap_P = 0;

static sigset_t all_masked;

#ifdef OPENSSL_BN_ASM_MONT
int bn_mul_mont(BN_ULONG *rp, const BN_ULONG *ap, const BN_ULONG *bp,
                const BN_ULONG *np, const BN_ULONG *n0, int num)
{
    int bn_mul_mont_int(BN_ULONG *rp, const BN_ULONG *ap, const BN_ULONG *bp,
                        const BN_ULONG *np, const BN_ULONG *n0, int num);
    int bn_mul4x_mont_int(BN_ULONG *rp, const BN_ULONG *ap, const BN_ULONG *bp,
                          const BN_ULONG *np, const BN_ULONG *n0, int num);

    if (num < 4)
        return 0;

    if ((num & 3) == 0)
        return bn_mul4x_mont_int(rp, ap, bp, np, n0, num);

    /*
     * There used to be [optional] call to bn_mul_mont_fpu64 here,
     * but above subroutine is faster on contemporary processors.
     * Formulation means that there might be old processors where
     * FPU code path would be faster, POWER6 perhaps, but there was
     * no opportunity to figure it out...
     */

    return bn_mul_mont_int(rp, ap, bp, np, n0, num);
}
#endif

void sha256_block_p8(void *ctx, const void *inp, size_t len);
void sha256_block_ppc(void *ctx, const void *inp, size_t len);
void sha256_block_data_order(void *ctx, const void *inp, size_t len);
void sha256_block_data_order(void *ctx, const void *inp, size_t len)
{
    OPENSSL_ppccap_P & PPC_CRYPTO207 ? sha256_block_p8(ctx, inp, len) :
        sha256_block_ppc(ctx, inp, len);
}

void sha512_block_p8(void *ctx, const void *inp, size_t len);
void sha512_block_ppc(void *ctx, const void *inp, size_t len);
void sha512_block_data_order(void *ctx, const void *inp, size_t len);
void sha512_block_data_order(void *ctx, const void *inp, size_t len)
{
    OPENSSL_ppccap_P & PPC_CRYPTO207 ? sha512_block_p8(ctx, inp, len) :
        sha512_block_ppc(ctx, inp, len);
}

#ifndef OPENSSL_NO_CHACHA
void ChaCha20_ctr32_int(unsigned char *out, const unsigned char *inp,
                        size_t len, const unsigned int key[8],
                        const unsigned int counter[4]);
void ChaCha20_ctr32_vmx(unsigned char *out, const unsigned char *inp,
                        size_t len, const unsigned int key[8],
                        const unsigned int counter[4]);
void ChaCha20_ctr32_vsx(unsigned char *out, const unsigned char *inp,
                        size_t len, const unsigned int key[8],
                        const unsigned int counter[4]);
void ChaCha20_ctr32(unsigned char *out, const unsigned char *inp,
                    size_t len, const unsigned int key[8],
                    const unsigned int counter[4])
{
    OPENSSL_ppccap_P & PPC_CRYPTO207
        ? ChaCha20_ctr32_vsx(out, inp, len, key, counter)
        : OPENSSL_ppccap_P & PPC_ALTIVEC
            ? ChaCha20_ctr32_vmx(out, inp, len, key, counter)
            : ChaCha20_ctr32_int(out, inp, len, key, counter);
}
#endif

#ifndef OPENSSL_NO_POLY1305
void poly1305_init_int(void *ctx, const unsigned char key[16]);
void poly1305_blocks(void *ctx, const unsigned char *inp, size_t len,
                         unsigned int padbit);
void poly1305_emit(void *ctx, unsigned char mac[16],
                       const unsigned int nonce[4]);
void poly1305_init_fpu(void *ctx, const unsigned char key[16]);
void poly1305_blocks_fpu(void *ctx, const unsigned char *inp, size_t len,
                         unsigned int padbit);
void poly1305_emit_fpu(void *ctx, unsigned char mac[16],
                       const unsigned int nonce[4]);
int poly1305_init(void *ctx, const unsigned char key[16], void *func[2]);
int poly1305_init(void *ctx, const unsigned char key[16], void *func[2])
{
    if (sizeof(size_t) == 4 && (OPENSSL_ppccap_P & PPC_FPU)) {
        poly1305_init_fpu(ctx, key);
        func[0] = (void*)(uintptr_t)poly1305_blocks_fpu;
        func[1] = (void*)(uintptr_t)poly1305_emit_fpu;
    } else {
        poly1305_init_int(ctx, key);
        func[0] = (void*)(uintptr_t)poly1305_blocks;
        func[1] = (void*)(uintptr_t)poly1305_emit;
    }
    return 1;
}
#endif

#ifdef ECP_NISTZ256_ASM
void ecp_nistz256_mul_mont(unsigned long res[4], const unsigned long a[4],
                           const unsigned long b[4]);

void ecp_nistz256_to_mont(unsigned long res[4], const unsigned long in[4]);
void ecp_nistz256_to_mont(unsigned long res[4], const unsigned long in[4])
{
    static const unsigned long RR[] = { 0x0000000000000003U,
                                        0xfffffffbffffffffU,
                                        0xfffffffffffffffeU,
                                        0x00000004fffffffdU };

    ecp_nistz256_mul_mont(res, in, RR);
}

void ecp_nistz256_from_mont(unsigned long res[4], const unsigned long in[4]);
void ecp_nistz256_from_mont(unsigned long res[4], const unsigned long in[4])
{
    static const unsigned long one[] = { 1, 0, 0, 0 };

    ecp_nistz256_mul_mont(res, in, one);
}
#endif

static sigjmp_buf ill_jmp;
static void ill_handler(int sig)
{
    siglongjmp(ill_jmp, sig);
}

void OPENSSL_fpu_probe(void);
void OPENSSL_ppc64_probe(void);
void OPENSSL_altivec_probe(void);
void OPENSSL_crypto207_probe(void);
void OPENSSL_madd300_probe(void);

long OPENSSL_rdtsc_mftb(void);
long OPENSSL_rdtsc_mfspr268(void);

uint32_t OPENSSL_rdtsc(void)
{
    if (OPENSSL_ppccap_P & PPC_MFTB)
        return OPENSSL_rdtsc_mftb();
    else if (OPENSSL_ppccap_P & PPC_MFSPR268)
        return OPENSSL_rdtsc_mfspr268();
    else
        return 0;
}

size_t OPENSSL_instrument_bus_mftb(unsigned int *, size_t);
size_t OPENSSL_instrument_bus_mfspr268(unsigned int *, size_t);

size_t OPENSSL_instrument_bus(unsigned int *out, size_t cnt)
{
    if (OPENSSL_ppccap_P & PPC_MFTB)
        return OPENSSL_instrument_bus_mftb(out, cnt);
    else if (OPENSSL_ppccap_P & PPC_MFSPR268)
        return OPENSSL_instrument_bus_mfspr268(out, cnt);
    else
        return 0;
}

size_t OPENSSL_instrument_bus2_mftb(unsigned int *, size_t, size_t);
size_t OPENSSL_instrument_bus2_mfspr268(unsigned int *, size_t, size_t);

size_t OPENSSL_instrument_bus2(unsigned int *out, size_t cnt, size_t max)
{
    if (OPENSSL_ppccap_P & PPC_MFTB)
        return OPENSSL_instrument_bus2_mftb(out, cnt, max);
    else if (OPENSSL_ppccap_P & PPC_MFSPR268)
        return OPENSSL_instrument_bus2_mfspr268(out, cnt, max);
    else
        return 0;
}

#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
# if __GLIBC_PREREQ(2, 16)
#  include <sys/auxv.h>
#  define OSSL_IMPLEMENT_GETAUXVAL
# endif
#endif

/* I wish <sys/auxv.h> was universally available */
#define HWCAP                   16      /* AT_HWCAP */
#define HWCAP_PPC64             (1U << 30)
#define HWCAP_ALTIVEC           (1U << 28)
#define HWCAP_FPU               (1U << 27)
#define HWCAP_POWER6_EXT        (1U << 9)
#define HWCAP_VSX               (1U << 7)

#define HWCAP2                  26      /* AT_HWCAP2 */
#define HWCAP_VEC_CRYPTO        (1U << 25)
#define HWCAP_ARCH_3_00         (1U << 23)

# if defined(__GNUC__) && __GNUC__>=2
__attribute__ ((constructor))
# endif
void OPENSSL_cpuid_setup(void)
{
    char *e;
    struct sigaction ill_oact, ill_act;
    sigset_t oset;
    static int trigger = 0;

    if (trigger)
        return;
    trigger = 1;

    if ((e = getenv("OPENSSL_ppccap"))) {
        OPENSSL_ppccap_P = strtoul(e, NULL, 0);
        return;
    }

    OPENSSL_ppccap_P = 0;

#if defined(_AIX)
    OPENSSL_ppccap_P |= PPC_FPU;

    if (sizeof(size_t) == 4) {
        struct utsname uts;
# if defined(_SC_AIX_KERNEL_BITMODE)
        if (sysconf(_SC_AIX_KERNEL_BITMODE) != 64)
            return;
# endif
        if (uname(&uts) != 0 || atoi(uts.version) < 6)
            return;
    }

# if defined(__power_set)
    /*
     * Value used in __power_set is a single-bit 1<<n one denoting
     * specific processor class. Incidentally 0xffffffff<<n can be
     * used to denote specific processor and its successors.
     */
    if (sizeof(size_t) == 4) {
        /* In 32-bit case PPC_FPU64 is always fastest [if option] */
        if (__power_set(0xffffffffU<<13))       /* POWER5 and later */
            OPENSSL_ppccap_P |= PPC_FPU64;
    } else {
        /* In 64-bit case PPC_FPU64 is fastest only on POWER6 */
        if (__power_set(0x1U<<14))              /* POWER6 */
            OPENSSL_ppccap_P |= PPC_FPU64;
    }

    if (__power_set(0xffffffffU<<14))           /* POWER6 and later */
        OPENSSL_ppccap_P |= PPC_ALTIVEC;

    if (__power_set(0xffffffffU<<16))           /* POWER8 and later */
        OPENSSL_ppccap_P |= PPC_CRYPTO207;

    if (__power_set(0xffffffffU<<17))           /* POWER9 and later */
        OPENSSL_ppccap_P |= PPC_MADD300;

    return;
# endif
#endif

#if defined(__APPLE__) && defined(__MACH__)
    OPENSSL_ppccap_P |= PPC_FPU;

    {
        int val;
        size_t len = sizeof(val);

        if (sysctlbyname("hw.optional.64bitops", &val, &len, NULL, 0) == 0) {
            if (val)
                OPENSSL_ppccap_P |= PPC_FPU64;
        }

        len = sizeof(val);
        if (sysctlbyname("hw.optional.altivec", &val, &len, NULL, 0) == 0) {
            if (val)
                OPENSSL_ppccap_P |= PPC_ALTIVEC;
        }

        return;
    }
#endif

#ifdef OSSL_IMPLEMENT_GETAUXVAL
    {
        unsigned long hwcap = getauxval(HWCAP);

        if (hwcap & HWCAP_FPU) {
            OPENSSL_ppccap_P |= PPC_FPU;

            if (sizeof(size_t) == 4) {
                /* In 32-bit case PPC_FPU64 is always fastest [if option] */
                if (hwcap & HWCAP_PPC64)
                    OPENSSL_ppccap_P |= PPC_FPU64;
            } else {
                /* In 64-bit case PPC_FPU64 is fastest only on POWER6 */
                if (hwcap & HWCAP_POWER6_EXT)
                    OPENSSL_ppccap_P |= PPC_FPU64;
            }
        }

        if (hwcap & HWCAP_ALTIVEC) {
            OPENSSL_ppccap_P |= PPC_ALTIVEC;

            if ((hwcap & HWCAP_VSX) && (getauxval(HWCAP2) & HWCAP_VEC_CRYPTO))
                OPENSSL_ppccap_P |= PPC_CRYPTO207;
        }

        if (hwcap & HWCAP_ARCH_3_00) {
            OPENSSL_ppccap_P |= PPC_MADD300;
        }
    }
#endif

    sigfillset(&all_masked);
    sigdelset(&all_masked, SIGILL);
    sigdelset(&all_masked, SIGTRAP);
#ifdef SIGEMT
    sigdelset(&all_masked, SIGEMT);
#endif
    sigdelset(&all_masked, SIGFPE);
    sigdelset(&all_masked, SIGBUS);
    sigdelset(&all_masked, SIGSEGV);

    memset(&ill_act, 0, sizeof(ill_act));
    ill_act.sa_handler = ill_handler;
    ill_act.sa_mask = all_masked;

    sigprocmask(SIG_SETMASK, &ill_act.sa_mask, &oset);
    sigaction(SIGILL, &ill_act, &ill_oact);

#ifndef OSSL_IMPLEMENT_GETAUXVAL
    if (sigsetjmp(ill_jmp,1) == 0) {
        OPENSSL_fpu_probe();
        OPENSSL_ppccap_P |= PPC_FPU;

        if (sizeof(size_t) == 4) {
# ifdef __linux
            struct utsname uts;
            if (uname(&uts) == 0 && strcmp(uts.machine, "ppc64") == 0)
# endif
                if (sigsetjmp(ill_jmp, 1) == 0) {
                    OPENSSL_ppc64_probe();
                    OPENSSL_ppccap_P |= PPC_FPU64;
                }
        } else {
            /*
             * Wanted code detecting POWER6 CPU and setting PPC_FPU64
             */
        }
    }

    if (sigsetjmp(ill_jmp, 1) == 0) {
        OPENSSL_altivec_probe();
        OPENSSL_ppccap_P |= PPC_ALTIVEC;
        if (sigsetjmp(ill_jmp, 1) == 0) {
            OPENSSL_crypto207_probe();
            OPENSSL_ppccap_P |= PPC_CRYPTO207;
        }
    }

    if (sigsetjmp(ill_jmp, 1) == 0) {
        OPENSSL_madd300_probe();
        OPENSSL_ppccap_P |= PPC_MADD300;
    }
#endif

    if (sigsetjmp(ill_jmp, 1) == 0) {
        OPENSSL_rdtsc_mftb();
        OPENSSL_ppccap_P |= PPC_MFTB;
    } else if (sigsetjmp(ill_jmp, 1) == 0) {
        OPENSSL_rdtsc_mfspr268();
        OPENSSL_ppccap_P |= PPC_MFSPR268;
    }

    sigaction(SIGILL, &ill_oact, NULL);
    sigprocmask(SIG_SETMASK, &oset, NULL);
}
