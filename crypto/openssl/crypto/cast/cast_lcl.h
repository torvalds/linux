/*
 * Copyright 1995-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifdef OPENSSL_SYS_WIN32
# include <stdlib.h>
#endif

#undef c2l
#define c2l(c,l)        (l =((unsigned long)(*((c)++)))    , \
                         l|=((unsigned long)(*((c)++)))<< 8L, \
                         l|=((unsigned long)(*((c)++)))<<16L, \
                         l|=((unsigned long)(*((c)++)))<<24L)

/* NOTE - c is not incremented as per c2l */
#undef c2ln
#define c2ln(c,l1,l2,n) { \
                        c+=n; \
                        l1=l2=0; \
                        switch (n) { \
                        case 8: l2 =((unsigned long)(*(--(c))))<<24L; \
                        case 7: l2|=((unsigned long)(*(--(c))))<<16L; \
                        case 6: l2|=((unsigned long)(*(--(c))))<< 8L; \
                        case 5: l2|=((unsigned long)(*(--(c))));     \
                        case 4: l1 =((unsigned long)(*(--(c))))<<24L; \
                        case 3: l1|=((unsigned long)(*(--(c))))<<16L; \
                        case 2: l1|=((unsigned long)(*(--(c))))<< 8L; \
                        case 1: l1|=((unsigned long)(*(--(c))));     \
                                } \
                        }

#undef l2c
#define l2c(l,c)        (*((c)++)=(unsigned char)(((l)     )&0xff), \
                         *((c)++)=(unsigned char)(((l)>> 8L)&0xff), \
                         *((c)++)=(unsigned char)(((l)>>16L)&0xff), \
                         *((c)++)=(unsigned char)(((l)>>24L)&0xff))

/* NOTE - c is not incremented as per l2c */
#undef l2cn
#define l2cn(l1,l2,c,n) { \
                        c+=n; \
                        switch (n) { \
                        case 8: *(--(c))=(unsigned char)(((l2)>>24L)&0xff); \
                        case 7: *(--(c))=(unsigned char)(((l2)>>16L)&0xff); \
                        case 6: *(--(c))=(unsigned char)(((l2)>> 8L)&0xff); \
                        case 5: *(--(c))=(unsigned char)(((l2)     )&0xff); \
                        case 4: *(--(c))=(unsigned char)(((l1)>>24L)&0xff); \
                        case 3: *(--(c))=(unsigned char)(((l1)>>16L)&0xff); \
                        case 2: *(--(c))=(unsigned char)(((l1)>> 8L)&0xff); \
                        case 1: *(--(c))=(unsigned char)(((l1)     )&0xff); \
                                } \
                        }

/* NOTE - c is not incremented as per n2l */
#define n2ln(c,l1,l2,n) { \
                        c+=n; \
                        l1=l2=0; \
                        switch (n) { \
                        case 8: l2 =((unsigned long)(*(--(c))))    ; \
                        /* fall thru */                              \
                        case 7: l2|=((unsigned long)(*(--(c))))<< 8; \
                        /* fall thru */                              \
                        case 6: l2|=((unsigned long)(*(--(c))))<<16; \
                        /* fall thru */                              \
                        case 5: l2|=((unsigned long)(*(--(c))))<<24; \
                        /* fall thru */                              \
                        case 4: l1 =((unsigned long)(*(--(c))))    ; \
                        /* fall thru */                              \
                        case 3: l1|=((unsigned long)(*(--(c))))<< 8; \
                        /* fall thru */                              \
                        case 2: l1|=((unsigned long)(*(--(c))))<<16; \
                        /* fall thru */                              \
                        case 1: l1|=((unsigned long)(*(--(c))))<<24; \
                                } \
                        }

/* NOTE - c is not incremented as per l2n */
#define l2nn(l1,l2,c,n) { \
                        c+=n; \
                        switch (n) { \
                        case 8: *(--(c))=(unsigned char)(((l2)    )&0xff); \
                        /* fall thru */                                    \
                        case 7: *(--(c))=(unsigned char)(((l2)>> 8)&0xff); \
                        /* fall thru */                                    \
                        case 6: *(--(c))=(unsigned char)(((l2)>>16)&0xff); \
                        /* fall thru */                                    \
                        case 5: *(--(c))=(unsigned char)(((l2)>>24)&0xff); \
                        /* fall thru */                                    \
                        case 4: *(--(c))=(unsigned char)(((l1)    )&0xff); \
                        /* fall thru */                                    \
                        case 3: *(--(c))=(unsigned char)(((l1)>> 8)&0xff); \
                        /* fall thru */                                    \
                        case 2: *(--(c))=(unsigned char)(((l1)>>16)&0xff); \
                        /* fall thru */                                    \
                        case 1: *(--(c))=(unsigned char)(((l1)>>24)&0xff); \
                                } \
                        }

#undef n2l
#define n2l(c,l)        (l =((unsigned long)(*((c)++)))<<24L, \
                         l|=((unsigned long)(*((c)++)))<<16L, \
                         l|=((unsigned long)(*((c)++)))<< 8L, \
                         l|=((unsigned long)(*((c)++))))

#undef l2n
#define l2n(l,c)        (*((c)++)=(unsigned char)(((l)>>24L)&0xff), \
                         *((c)++)=(unsigned char)(((l)>>16L)&0xff), \
                         *((c)++)=(unsigned char)(((l)>> 8L)&0xff), \
                         *((c)++)=(unsigned char)(((l)     )&0xff))

#if defined(OPENSSL_SYS_WIN32) && defined(_MSC_VER)
# define ROTL(a,n)     (_lrotl(a,n))
#else
# define ROTL(a,n)     ((((a)<<(n))&0xffffffffL)|((a)>>((32-(n))&31)))
#endif

#define C_M    0x3fc
#define C_0    22L
#define C_1    14L
#define C_2     6L
#define C_3     2L              /* left shift */

/* The rotate has an extra 16 added to it to help the x86 asm */
#if defined(CAST_PTR)
# define E_CAST(n,key,L,R,OP1,OP2,OP3) \
        { \
        int i; \
        t=(key[n*2] OP1 R)&0xffffffffL; \
        i=key[n*2+1]; \
        t=ROTL(t,i); \
        L^= (((((*(CAST_LONG *)((unsigned char *) \
                        CAST_S_table0+((t>>C_2)&C_M)) OP2 \
                *(CAST_LONG *)((unsigned char *) \
                        CAST_S_table1+((t<<C_3)&C_M)))&0xffffffffL) OP3 \
                *(CAST_LONG *)((unsigned char *) \
                        CAST_S_table2+((t>>C_0)&C_M)))&0xffffffffL) OP1 \
                *(CAST_LONG *)((unsigned char *) \
                        CAST_S_table3+((t>>C_1)&C_M)))&0xffffffffL; \
        }
#elif defined(CAST_PTR2)
# define E_CAST(n,key,L,R,OP1,OP2,OP3) \
        { \
        int i; \
        CAST_LONG u,v,w; \
        w=(key[n*2] OP1 R)&0xffffffffL; \
        i=key[n*2+1]; \
        w=ROTL(w,i); \
        u=w>>C_2; \
        v=w<<C_3; \
        u&=C_M; \
        v&=C_M; \
        t= *(CAST_LONG *)((unsigned char *)CAST_S_table0+u); \
        u=w>>C_0; \
        t=(t OP2 *(CAST_LONG *)((unsigned char *)CAST_S_table1+v))&0xffffffffL;\
        v=w>>C_1; \
        u&=C_M; \
        v&=C_M; \
        t=(t OP3 *(CAST_LONG *)((unsigned char *)CAST_S_table2+u)&0xffffffffL);\
        t=(t OP1 *(CAST_LONG *)((unsigned char *)CAST_S_table3+v)&0xffffffffL);\
        L^=(t&0xffffffff); \
        }
#else
# define E_CAST(n,key,L,R,OP1,OP2,OP3) \
        { \
        CAST_LONG a,b,c,d; \
        t=(key[n*2] OP1 R)&0xffffffff; \
        t=ROTL(t,(key[n*2+1])); \
        a=CAST_S_table0[(t>> 8)&0xff]; \
        b=CAST_S_table1[(t    )&0xff]; \
        c=CAST_S_table2[(t>>24)&0xff]; \
        d=CAST_S_table3[(t>>16)&0xff]; \
        L^=(((((a OP2 b)&0xffffffffL) OP3 c)&0xffffffffL) OP1 d)&0xffffffffL; \
        }
#endif

extern const CAST_LONG CAST_S_table0[256];
extern const CAST_LONG CAST_S_table1[256];
extern const CAST_LONG CAST_S_table2[256];
extern const CAST_LONG CAST_S_table3[256];
extern const CAST_LONG CAST_S_table4[256];
extern const CAST_LONG CAST_S_table5[256];
extern const CAST_LONG CAST_S_table6[256];
extern const CAST_LONG CAST_S_table7[256];
