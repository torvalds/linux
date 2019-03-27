/*
 * Copyright 2012-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef __SPARC_ARCH_H__
# define __SPARC_ARCH_H__

# define SPARCV9_TICK_PRIVILEGED (1<<0)
# define SPARCV9_PREFER_FPU      (1<<1)
# define SPARCV9_VIS1            (1<<2)
# define SPARCV9_VIS2            (1<<3)/* reserved */
# define SPARCV9_FMADD           (1<<4)
# define SPARCV9_BLK             (1<<5)/* VIS1 block copy */
# define SPARCV9_VIS3            (1<<6)
# define SPARCV9_RANDOM          (1<<7)
# define SPARCV9_64BIT_STACK     (1<<8)
# define SPARCV9_FJAESX          (1<<9)/* Fujitsu SPARC64 X AES */
# define SPARCV9_FJDESX          (1<<10)/* Fujitsu SPARC64 X DES, reserved */
# define SPARCV9_FJHPCACE        (1<<11)/* Fujitsu HPC-ACE, reserved */
# define SPARCV9_IMA             (1<<13)/* reserved */
# define SPARCV9_VIS4            (1<<14)/* reserved */

/*
 * OPENSSL_sparcv9cap_P[1] is copy of Compatibility Feature Register,
 * %asr26, SPARC-T4 and later. There is no SPARCV9_CFR bit in
 * OPENSSL_sparcv9cap_P[0], as %cfr copy is sufficient...
 */
# define CFR_AES         0x00000001/* Supports AES opcodes */
# define CFR_DES         0x00000002/* Supports DES opcodes */
# define CFR_KASUMI      0x00000004/* Supports KASUMI opcodes */
# define CFR_CAMELLIA    0x00000008/* Supports CAMELLIA opcodes */
# define CFR_MD5         0x00000010/* Supports MD5 opcodes */
# define CFR_SHA1        0x00000020/* Supports SHA1 opcodes */
# define CFR_SHA256      0x00000040/* Supports SHA256 opcodes */
# define CFR_SHA512      0x00000080/* Supports SHA512 opcodes */
# define CFR_MPMUL       0x00000100/* Supports MPMUL opcodes */
# define CFR_MONTMUL     0x00000200/* Supports MONTMUL opcodes */
# define CFR_MONTSQR     0x00000400/* Supports MONTSQR opcodes */
# define CFR_CRC32C      0x00000800/* Supports CRC32C opcodes */
# define CFR_XMPMUL      0x00001000/* Supports XMPMUL opcodes */
# define CFR_XMONTMUL    0x00002000/* Supports XMONTMUL opcodes */
# define CFR_XMONTSQR    0x00004000/* Supports XMONTSQR opcodes */

# if defined(OPENSSL_PIC) && !defined(__PIC__)
#  define __PIC__
# endif

# if defined(__SUNPRO_C) && defined(__sparcv9) && !defined(__arch64__)
#  define __arch64__
# endif

# define SPARC_PIC_THUNK(reg)    \
        .align  32;             \
.Lpic_thunk:                    \
        jmp     %o7 + 8;        \
         add    %o7, reg, reg;

# define SPARC_PIC_THUNK_CALL(reg)                       \
        sethi   %hi(_GLOBAL_OFFSET_TABLE_-4), reg;      \
        call    .Lpic_thunk;                            \
         or     reg, %lo(_GLOBAL_OFFSET_TABLE_+4), reg;

# if 1
#  define SPARC_SETUP_GOT_REG(reg)       SPARC_PIC_THUNK_CALL(reg)
# else
#  define SPARC_SETUP_GOT_REG(reg)       \
        sethi   %hi(_GLOBAL_OFFSET_TABLE_-4), reg;      \
        call    .+8;                                    \
        or      reg,%lo(_GLOBAL_OFFSET_TABLE_+4), reg;  \
        add     %o7, reg, reg
# endif

# if defined(__arch64__)

#  define SPARC_LOAD_ADDRESS(SYM, reg)   \
        setx    SYM, %o7, reg;
#  define LDPTR          ldx
#  define SIZE_T_CC      %xcc
#  define STACK_FRAME    192
#  define STACK_BIAS     2047
#  define STACK_7thARG   (STACK_BIAS+176)

# else

#  define SPARC_LOAD_ADDRESS(SYM, reg)   \
        set     SYM, reg;
#  define LDPTR          ld
#  define SIZE_T_CC      %icc
#  define STACK_FRAME    112
#  define STACK_BIAS     0
#  define STACK_7thARG   92
#  define SPARC_LOAD_ADDRESS_LEAF(SYM,reg,tmp) SPARC_LOAD_ADDRESS(SYM,reg)

# endif

# ifdef __PIC__
#  undef SPARC_LOAD_ADDRESS
#  undef SPARC_LOAD_ADDRESS_LEAF
#  define SPARC_LOAD_ADDRESS(SYM, reg)   \
        SPARC_SETUP_GOT_REG(reg);       \
        sethi   %hi(SYM), %o7;          \
        or      %o7, %lo(SYM), %o7;     \
        LDPTR   [reg + %o7], reg;
# endif

# ifndef SPARC_LOAD_ADDRESS_LEAF
#  define SPARC_LOAD_ADDRESS_LEAF(SYM, reg, tmp) \
        mov     %o7, tmp;                       \
        SPARC_LOAD_ADDRESS(SYM, reg)            \
        mov     tmp, %o7;
# endif

#endif                          /* __SPARC_ARCH_H__ */
