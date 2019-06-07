/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _GDTH_IOCTL_H
#define _GDTH_IOCTL_H

/* gdth_ioctl.h
 * $Id: gdth_ioctl.h,v 1.14 2004/02/19 15:43:15 achim Exp $
 */

/* IOCTLs */
#define GDTIOCTL_MASK       ('J'<<8)
#define GDTIOCTL_GENERAL    (GDTIOCTL_MASK | 0) /* general IOCTL */
#define GDTIOCTL_DRVERS     (GDTIOCTL_MASK | 1) /* get driver version */
#define GDTIOCTL_CTRTYPE    (GDTIOCTL_MASK | 2) /* get controller type */
#define GDTIOCTL_OSVERS     (GDTIOCTL_MASK | 3) /* get OS version */
#define GDTIOCTL_HDRLIST    (GDTIOCTL_MASK | 4) /* get host drive list */
#define GDTIOCTL_CTRCNT     (GDTIOCTL_MASK | 5) /* get controller count */
#define GDTIOCTL_LOCKDRV    (GDTIOCTL_MASK | 6) /* lock host drive */
#define GDTIOCTL_LOCKCHN    (GDTIOCTL_MASK | 7) /* lock channel */
#define GDTIOCTL_EVENT      (GDTIOCTL_MASK | 8) /* read controller events */
#define GDTIOCTL_SCSI       (GDTIOCTL_MASK | 9) /* SCSI command */
#define GDTIOCTL_RESET_BUS  (GDTIOCTL_MASK |10) /* reset SCSI bus */
#define GDTIOCTL_RESCAN     (GDTIOCTL_MASK |11) /* rescan host drives */
#define GDTIOCTL_RESET_DRV  (GDTIOCTL_MASK |12) /* reset (remote) drv. res. */

#define GDTIOCTL_MAGIC  0xaffe0004
#define EVENT_SIZE      294 
#define GDTH_MAXSG      32                      /* max. s/g elements */

#define MAX_LDRIVES     255                     /* max. log. drive count */
#define MAX_HDRIVES     MAX_LDRIVES             /* max. host drive count */

/* scatter/gather element */
typedef struct {
    u32     sg_ptr;                         /* address */
    u32     sg_len;                         /* length */
} __attribute__((packed)) gdth_sg_str;

/* scatter/gather element - 64bit addresses */
typedef struct {
    u64     sg_ptr;                         /* address */
    u32     sg_len;                         /* length */
} __attribute__((packed)) gdth_sg64_str;

/* command structure */
typedef struct {
    u32     BoardNode;                      /* board node (always 0) */
    u32     CommandIndex;                   /* command number */
    u16      OpCode;                         /* the command (READ,..) */
    union {
        struct {
            u16      DeviceNo;               /* number of cache drive */
            u32     BlockNo;                /* block number */
            u32     BlockCnt;               /* block count */
            u32     DestAddr;               /* dest. addr. (if s/g: -1) */
            u32     sg_canz;                /* s/g element count */
            gdth_sg_str sg_lst[GDTH_MAXSG];     /* s/g list */
        } __attribute__((packed)) cache;                         /* cache service cmd. str. */
        struct {
            u16      DeviceNo;               /* number of cache drive */
            u64     BlockNo;                /* block number */
            u32     BlockCnt;               /* block count */
            u64     DestAddr;               /* dest. addr. (if s/g: -1) */
            u32     sg_canz;                /* s/g element count */
            gdth_sg64_str sg_lst[GDTH_MAXSG];   /* s/g list */
        } __attribute__((packed)) cache64;                       /* cache service cmd. str. */
        struct {
            u16      param_size;             /* size of p_param buffer */
            u32     subfunc;                /* IOCTL function */
            u32     channel;                /* device */
            u64     p_param;                /* buffer */
        } __attribute__((packed)) ioctl;                         /* IOCTL command structure */
        struct {
            u16      reserved;
            union {
                struct {
                    u32  msg_handle;        /* message handle */
                    u64  msg_addr;          /* message buffer address */
                } __attribute__((packed)) msg;
                u8       data[12];          /* buffer for rtc data, ... */
            } su;
        } __attribute__((packed)) screen;                        /* screen service cmd. str. */
        struct {
            u16      reserved;
            u32     direction;              /* data direction */
            u32     mdisc_time;             /* disc. time (0: no timeout)*/
            u32     mcon_time;              /* connect time(0: no to.) */
            u32     sdata;                  /* dest. addr. (if s/g: -1) */
            u32     sdlen;                  /* data length (bytes) */
            u32     clen;                   /* SCSI cmd. length(6,10,12) */
            u8      cmd[12];                /* SCSI command */
            u8      target;                 /* target ID */
            u8      lun;                    /* LUN */
            u8      bus;                    /* SCSI bus number */
            u8      priority;               /* only 0 used */
            u32     sense_len;              /* sense data length */
            u32     sense_data;             /* sense data addr. */
            u32     link_p;                 /* linked cmds (not supp.) */
            u32     sg_ranz;                /* s/g element count */
            gdth_sg_str sg_lst[GDTH_MAXSG];     /* s/g list */
        } __attribute__((packed)) raw;                           /* raw service cmd. struct. */
        struct {
            u16      reserved;
            u32     direction;              /* data direction */
            u32     mdisc_time;             /* disc. time (0: no timeout)*/
            u32     mcon_time;              /* connect time(0: no to.) */
            u64     sdata;                  /* dest. addr. (if s/g: -1) */
            u32     sdlen;                  /* data length (bytes) */
            u32     clen;                   /* SCSI cmd. length(6,..,16) */
            u8      cmd[16];                /* SCSI command */
            u8      target;                 /* target ID */
            u8      lun;                    /* LUN */
            u8      bus;                    /* SCSI bus number */
            u8      priority;               /* only 0 used */
            u32     sense_len;              /* sense data length */
            u64     sense_data;             /* sense data addr. */
            u32     sg_ranz;                /* s/g element count */
            gdth_sg64_str sg_lst[GDTH_MAXSG];   /* s/g list */
        } __attribute__((packed)) raw64;                         /* raw service cmd. struct. */
    } u;
    /* additional variables */
    u8      Service;                        /* controller service */
    u8      reserved;
    u16      Status;                         /* command result */
    u32     Info;                           /* additional information */
    void        *RequestBuffer;                 /* request buffer */
} __attribute__((packed)) gdth_cmd_str;

/* controller event structure */
#define ES_ASYNC    1
#define ES_DRIVER   2
#define ES_TEST     3
#define ES_SYNC     4
typedef struct {
    u16                  size;               /* size of structure */
    union {
        char                stream[16];
        struct {
            u16          ionode;
            u16          service;
            u32         index;
        } __attribute__((packed)) driver;
        struct {
            u16          ionode;
            u16          service;
            u16          status;
            u32         info;
            u8          scsi_coord[3];
        } __attribute__((packed)) async;
        struct {
            u16          ionode;
            u16          service;
            u16          status;
            u32         info;
            u16          hostdrive;
            u8          scsi_coord[3];
            u8          sense_key;
        } __attribute__((packed)) sync;
        struct {
            u32         l1, l2, l3, l4;
        } __attribute__((packed)) test;
    } eu;
    u32                 severity;
    u8                  event_string[256];          
} __attribute__((packed)) gdth_evt_data;

typedef struct {
    u32         first_stamp;
    u32         last_stamp;
    u16          same_count;
    u16          event_source;
    u16          event_idx;
    u8          application;
    u8          reserved;
    gdth_evt_data   event_data;
} __attribute__((packed)) gdth_evt_str;

/* GDTIOCTL_GENERAL */
typedef struct {
    u16 ionode;                              /* controller number */
    u16 timeout;                             /* timeout */
    u32 info;                               /* error info */ 
    u16 status;                              /* status */
    unsigned long data_len;                             /* data buffer size */
    unsigned long sense_len;                            /* sense buffer size */
    gdth_cmd_str command;                       /* command */                   
} gdth_ioctl_general;

/* GDTIOCTL_LOCKDRV */
typedef struct {
    u16 ionode;                              /* controller number */
    u8 lock;                                /* lock/unlock */
    u8 drive_cnt;                           /* drive count */
    u16 drives[MAX_HDRIVES];                 /* drives */
} gdth_ioctl_lockdrv;

/* GDTIOCTL_LOCKCHN */
typedef struct {
    u16 ionode;                              /* controller number */
    u8 lock;                                /* lock/unlock */
    u8 channel;                             /* channel */
} gdth_ioctl_lockchn;

/* GDTIOCTL_OSVERS */
typedef struct {
    u8 version;                             /* OS version */
    u8 subversion;                          /* OS subversion */
    u16 revision;                            /* revision */
} gdth_ioctl_osvers;

/* GDTIOCTL_CTRTYPE */
typedef struct {
    u16 ionode;                              /* controller number */
    u8 type;                                /* controller type */
    u16 info;                                /* slot etc. */
    u16 oem_id;                              /* OEM ID */
    u16 bios_ver;                            /* not used */
    u16 access;                              /* not used */
    u16 ext_type;                            /* extended type */
    u16 device_id;                           /* device ID */
    u16 sub_device_id;                       /* sub device ID */
} gdth_ioctl_ctrtype;

/* GDTIOCTL_EVENT */
typedef struct {
    u16 ionode;
    int erase;                                  /* erase event? */
    int handle;                                 /* event handle */
    gdth_evt_str event;
} gdth_ioctl_event;

/* GDTIOCTL_RESCAN/GDTIOCTL_HDRLIST */
typedef struct {
    u16 ionode;                              /* controller number */
    u8 flag;                                /* add/remove */
    u16 hdr_no;                              /* drive no. */
    struct {
        u8 bus;                             /* SCSI bus */
        u8 target;                          /* target ID */
        u8 lun;                             /* LUN */
        u8 cluster_type;                    /* cluster properties */
    } hdr_list[MAX_HDRIVES];                    /* index is host drive number */
} gdth_ioctl_rescan;

/* GDTIOCTL_RESET_BUS/GDTIOCTL_RESET_DRV */
typedef struct {
    u16 ionode;                              /* controller number */
    u16 number;                              /* bus/host drive number */
    u16 status;                              /* status */
} gdth_ioctl_reset;

#endif
