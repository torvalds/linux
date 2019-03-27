#ifndef _RPL_H_

/*
 * NOTE: the contents of this file are an interpretation of RFC6550.
 *       no copyright is asserted on this file, as it transcribes
 *       a public specification.
 *
 */

/*
 * DIO: Updated to RFC6550, as published in 2012: section 6. (page 30)
 */

#define ND_RPL_MESSAGE 155  /* 0x9B */

enum ND_RPL_CODE {
    ND_RPL_DAG_IS=0x00,
    ND_RPL_DAG_IO=0x01,
    ND_RPL_DAO   =0x02,
    ND_RPL_DAO_ACK=0x03,
    ND_RPL_SEC_DAG_IS = 0x80,
    ND_RPL_SEC_DAG_IO = 0x81,
    ND_RPL_SEC_DAG    = 0x82,
    ND_RPL_SEC_DAG_ACK= 0x83,
    ND_RPL_SEC_CONSIST= 0x8A
};

enum ND_RPL_DIO_FLAGS {
        ND_RPL_DIO_GROUNDED = 0x80,
        ND_RPL_DIO_DATRIG   = 0x40,
        ND_RPL_DIO_DASUPPORT= 0x20,
        ND_RPL_DIO_RES4     = 0x10,
        ND_RPL_DIO_RES3     = 0x08,
        ND_RPL_DIO_PRF_MASK = 0x07  /* 3-bit preference */
};

#define DAGID_LEN 16

/* section 6 of draft-ietf-roll-rpl-19 */
struct nd_rpl_security {
    uint8_t  rpl_sec_t_reserved;     /* bit 7 is T-bit */
    uint8_t  rpl_sec_algo;
    uint16_t rpl_sec_kim_lvl_flags;  /* bit 15/14, KIM */
                                      /* bit 10-8, LVL, bit 7-0 flags */
    uint32_t rpl_sec_counter;
#if 0
    uint8_t  rpl_sec_ki[0];          /* depends upon kim */
#endif
};

/* section 6.2.1, DODAG Information Solication (DIS_IS) */
struct nd_rpl_dis_is {
    uint8_t rpl_dis_flags;
    uint8_t rpl_dis_reserved;
#if 0
    uint8_t rpl_dis_options[0];
#endif
};

/* section 6.3.1, DODAG Information Object (DIO) */
struct nd_rpl_dio {
    uint8_t  rpl_instanceid;
    uint8_t  rpl_version;
    uint16_t rpl_dagrank;
    uint8_t  rpl_mopprf;   /* bit 7=G, 5-3=MOP, 2-0=PRF */
    uint8_t  rpl_dtsn;     /* Dest. Advertisement Trigger Sequence Number */
    uint8_t  rpl_flags;    /* no flags defined yet */
    uint8_t  rpl_resv1;
    uint8_t  rpl_dagid[DAGID_LEN];
};
#define RPL_DIO_GROUND_FLAG 0x80
#define RPL_DIO_MOP_SHIFT   3
#define RPL_DIO_MOP_MASK    (7 << RPL_DIO_MOP_SHIFT)
#define RPL_DIO_PRF_SHIFT   0
#define RPL_DIO_PRF_MASK    (7 << RPL_DIO_PRF_SHIFT)
#define RPL_DIO_GROUNDED(X) ((X)&RPL_DIO_GROUND_FLAG)
#define RPL_DIO_MOP(X)      (enum RPL_DIO_MOP)(((X)&RPL_DIO_MOP_MASK) >> RPL_DIO_MOP_SHIFT)
#define RPL_DIO_PRF(X)      (((X)&RPL_DIO_PRF_MASK) >> RPL_DIO_PRF_SHIFT)

enum RPL_DIO_MOP {
    RPL_DIO_NONSTORING= 0x0,
    RPL_DIO_STORING   = 0x1,
    RPL_DIO_NONSTORING_MULTICAST = 0x2,
    RPL_DIO_STORING_MULTICAST    = 0x3
};

enum RPL_SUBOPT {
        RPL_OPT_PAD0        = 0,
        RPL_OPT_PADN        = 1,
        RPL_DIO_METRICS     = 2,
        RPL_DIO_ROUTINGINFO = 3,
        RPL_DIO_CONFIG      = 4,
        RPL_DAO_RPLTARGET   = 5,
        RPL_DAO_TRANSITINFO = 6,
        RPL_DIO_DESTPREFIX  = 8,
        RPL_DAO_RPLTARGET_DESC=9
};

struct rpl_dio_genoption {
    uint8_t rpl_dio_type;
    uint8_t rpl_dio_len;        /* suboption length, not including type/len */
};
#define RPL_DIO_GENOPTION_LEN	2

#define RPL_DIO_LIFETIME_INFINITE   0xffffffff
#define RPL_DIO_LIFETIME_DISCONNECT 0

struct rpl_dio_destprefix {
    uint8_t rpl_dio_type;
    uint8_t rpl_dio_len;
    uint8_t rpl_dio_prefixlen;        /* in bits */
    uint8_t rpl_dio_prf;              /* flags, including Route Preference */
    uint32_t rpl_dio_prefixlifetime;  /* in seconds */
#if 0
    uint8_t rpl_dio_prefix[0];        /* variable number of bytes */
#endif
};

/* section 6.4.1, DODAG Information Object (DIO) */
struct nd_rpl_dao {
    uint8_t  rpl_instanceid;
    uint8_t  rpl_flags;      /* bit 7=K, 6=D */
    uint8_t  rpl_resv;
    uint8_t  rpl_daoseq;
    uint8_t  rpl_dagid[DAGID_LEN];   /* present when D set. */
};
#define ND_RPL_DAO_MIN_LEN	4	/* length without DAGID */

/* indicates if this DAO is to be acK'ed */
#define RPL_DAO_K_SHIFT   7
#define RPL_DAO_K_MASK    (1 << RPL_DAO_K_SHIFT)
#define RPL_DAO_K(X)      (((X)&RPL_DAO_K_MASK) >> RPL_DAO_K_SHIFT)

/* indicates if the DAGID is present */
#define RPL_DAO_D_SHIFT   6
#define RPL_DAO_D_MASK    (1 << RPL_DAO_D_SHIFT)
#define RPL_DAO_D(X)      (((X)&RPL_DAO_D_MASK) >> RPL_DAO_D_SHIFT)

struct rpl_dao_target {
    uint8_t rpl_dao_type;
    uint8_t rpl_dao_len;
    uint8_t rpl_dao_flags;            /* unused */
    uint8_t rpl_dao_prefixlen;        /* in bits */
#if 0
    uint8_t rpl_dao_prefix[0];        /* variable number of bytes */
#endif
};

/* section 6.5.1, Destination Advertisement Object Acknowledgement (DAO-ACK) */
struct nd_rpl_daoack {
    uint8_t  rpl_instanceid;
    uint8_t  rpl_flags;      /* bit 7=D */
    uint8_t  rpl_daoseq;
    uint8_t  rpl_status;
    uint8_t  rpl_dagid[DAGID_LEN];   /* present when D set. */
};
#define ND_RPL_DAOACK_MIN_LEN	4	/* length without DAGID */
/* indicates if the DAGID is present */
#define RPL_DAOACK_D_SHIFT   7
#define RPL_DAOACK_D_MASK    (1 << RPL_DAOACK_D_SHIFT)
#define RPL_DAOACK_D(X)      (((X)&RPL_DAOACK_D_MASK) >> RPL_DAOACK_D_SHIFT)



#define _RPL_H_
#endif /* _RPL_H_ */

/*
 * Local Variables:
 * c-basic-offset:4
 * c-style: whitesmith
 * End:
 */

