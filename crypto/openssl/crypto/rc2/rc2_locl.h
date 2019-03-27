/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

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
                        /* fall thru */                               \
                        case 7: l2|=((unsigned long)(*(--(c))))<<16L; \
                        /* fall thru */                               \
                        case 6: l2|=((unsigned long)(*(--(c))))<< 8L; \
                        /* fall thru */                               \
                        case 5: l2|=((unsigned long)(*(--(c))));      \
                        /* fall thru */                               \
                        case 4: l1 =((unsigned long)(*(--(c))))<<24L; \
                        /* fall thru */                               \
                        case 3: l1|=((unsigned long)(*(--(c))))<<16L; \
                        /* fall thru */                               \
                        case 2: l1|=((unsigned long)(*(--(c))))<< 8L; \
                        /* fall thru */                               \
                        case 1: l1|=((unsigned long)(*(--(c))));      \
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
                        /* fall thru */                                     \
                        case 7: *(--(c))=(unsigned char)(((l2)>>16L)&0xff); \
                        /* fall thru */                                     \
                        case 6: *(--(c))=(unsigned char)(((l2)>> 8L)&0xff); \
                        /* fall thru */                                     \
                        case 5: *(--(c))=(unsigned char)(((l2)     )&0xff); \
                        /* fall thru */                                     \
                        case 4: *(--(c))=(unsigned char)(((l1)>>24L)&0xff); \
                        /* fall thru */                                     \
                        case 3: *(--(c))=(unsigned char)(((l1)>>16L)&0xff); \
                        /* fall thru */                                     \
                        case 2: *(--(c))=(unsigned char)(((l1)>> 8L)&0xff); \
                        /* fall thru */                                     \
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

#define C_RC2(n) \
        t=(x0+(x1& ~x3)+(x2&x3)+ *(p0++))&0xffff; \
        x0=(t<<1)|(t>>15); \
        t=(x1+(x2& ~x0)+(x3&x0)+ *(p0++))&0xffff; \
        x1=(t<<2)|(t>>14); \
        t=(x2+(x3& ~x1)+(x0&x1)+ *(p0++))&0xffff; \
        x2=(t<<3)|(t>>13); \
        t=(x3+(x0& ~x2)+(x1&x2)+ *(p0++))&0xffff; \
        x3=(t<<5)|(t>>11);
