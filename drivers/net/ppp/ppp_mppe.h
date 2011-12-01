#define MPPE_PAD                4      /* MPPE growth per frame */
#define MPPE_MAX_KEY_LEN       16      /* largest key length (128-bit) */

/* option bits for ccp_options.mppe */
#define MPPE_OPT_40            0x01    /* 40 bit */
#define MPPE_OPT_128           0x02    /* 128 bit */
#define MPPE_OPT_STATEFUL      0x04    /* stateful mode */
/* unsupported opts */
#define MPPE_OPT_56            0x08    /* 56 bit */
#define MPPE_OPT_MPPC          0x10    /* MPPC compression */
#define MPPE_OPT_D             0x20    /* Unknown */
#define MPPE_OPT_UNSUPPORTED (MPPE_OPT_56|MPPE_OPT_MPPC|MPPE_OPT_D)
#define MPPE_OPT_UNKNOWN       0x40    /* Bits !defined in RFC 3078 were set */

/*
 * This is not nice ... the alternative is a bitfield struct though.
 * And unfortunately, we cannot share the same bits for the option
 * names above since C and H are the same bit.  We could do a u_int32
 * but then we have to do a htonl() all the time and/or we still need
 * to know which octet is which.
 */
#define MPPE_C_BIT             0x01    /* MPPC */
#define MPPE_D_BIT             0x10    /* Obsolete, usage unknown */
#define MPPE_L_BIT             0x20    /* 40-bit */
#define MPPE_S_BIT             0x40    /* 128-bit */
#define MPPE_M_BIT             0x80    /* 56-bit, not supported */
#define MPPE_H_BIT             0x01    /* Stateless (in a different byte) */

/* Does not include H bit; used for least significant octet only. */
#define MPPE_ALL_BITS (MPPE_D_BIT|MPPE_L_BIT|MPPE_S_BIT|MPPE_M_BIT|MPPE_H_BIT)

/* Build a CI from mppe opts (see RFC 3078) */
#define MPPE_OPTS_TO_CI(opts, ci)              \
    do {                                       \
       u_char *ptr = ci; /* u_char[4] */       \
                                               \
       /* H bit */                             \
       if (opts & MPPE_OPT_STATEFUL)           \
           *ptr++ = 0x0;                       \
       else                                    \
           *ptr++ = MPPE_H_BIT;                \
       *ptr++ = 0;                             \
       *ptr++ = 0;                             \
                                               \
       /* S,L bits */                          \
       *ptr = 0;                               \
       if (opts & MPPE_OPT_128)                \
           *ptr |= MPPE_S_BIT;                 \
       if (opts & MPPE_OPT_40)                 \
           *ptr |= MPPE_L_BIT;                 \
       /* M,D,C bits not supported */          \
    } while (/* CONSTCOND */ 0)

/* The reverse of the above */
#define MPPE_CI_TO_OPTS(ci, opts)              \
    do {                                       \
       u_char *ptr = ci; /* u_char[4] */       \
                                               \
       opts = 0;                               \
                                               \
       /* H bit */                             \
       if (!(ptr[0] & MPPE_H_BIT))             \
           opts |= MPPE_OPT_STATEFUL;          \
                                               \
       /* S,L bits */                          \
       if (ptr[3] & MPPE_S_BIT)                \
           opts |= MPPE_OPT_128;               \
       if (ptr[3] & MPPE_L_BIT)                \
           opts |= MPPE_OPT_40;                \
                                               \
       /* M,D,C bits */                        \
       if (ptr[3] & MPPE_M_BIT)                \
           opts |= MPPE_OPT_56;                \
       if (ptr[3] & MPPE_D_BIT)                \
           opts |= MPPE_OPT_D;                 \
       if (ptr[3] & MPPE_C_BIT)                \
           opts |= MPPE_OPT_MPPC;              \
                                               \
       /* Other bits */                        \
       if (ptr[0] & ~MPPE_H_BIT)               \
           opts |= MPPE_OPT_UNKNOWN;           \
       if (ptr[1] || ptr[2])                   \
           opts |= MPPE_OPT_UNKNOWN;           \
       if (ptr[3] & ~MPPE_ALL_BITS)            \
           opts |= MPPE_OPT_UNKNOWN;           \
    } while (/* CONSTCOND */ 0)
