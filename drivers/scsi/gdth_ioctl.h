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
#ifdef GDTH_IOCTL_PROC
#define MAX_HDRIVES     100                     /* max. host drive count */
#else
#define MAX_HDRIVES     MAX_LDRIVES             /* max. host drive count */
#endif

/* typedefs */
#ifdef __KERNEL__
typedef u32     ulong32;
typedef u64     ulong64;
#endif

#define PACKED  __attribute__((packed))

/* scatter/gather element */
typedef struct {
    ulong32     sg_ptr;                         /* address */
    ulong32     sg_len;                         /* length */
} PACKED gdth_sg_str;

/* scatter/gather element - 64bit addresses */
typedef struct {
    ulong64     sg_ptr;                         /* address */
    ulong32     sg_len;                         /* length */
} PACKED gdth_sg64_str;

/* command structure */
typedef struct {
    ulong32     BoardNode;                      /* board node (always 0) */
    ulong32     CommandIndex;                   /* command number */
    ushort      OpCode;                         /* the command (READ,..) */
    union {
        struct {
            ushort      DeviceNo;               /* number of cache drive */
            ulong32     BlockNo;                /* block number */
            ulong32     BlockCnt;               /* block count */
            ulong32     DestAddr;               /* dest. addr. (if s/g: -1) */
            ulong32     sg_canz;                /* s/g element count */
            gdth_sg_str sg_lst[GDTH_MAXSG];     /* s/g list */
        } PACKED cache;                         /* cache service cmd. str. */
        struct {
            ushort      DeviceNo;               /* number of cache drive */
            ulong64     BlockNo;                /* block number */
            ulong32     BlockCnt;               /* block count */
            ulong64     DestAddr;               /* dest. addr. (if s/g: -1) */
            ulong32     sg_canz;                /* s/g element count */
            gdth_sg64_str sg_lst[GDTH_MAXSG];   /* s/g list */
        } PACKED cache64;                       /* cache service cmd. str. */
        struct {
            ushort      param_size;             /* size of p_param buffer */
            ulong32     subfunc;                /* IOCTL function */
            ulong32     channel;                /* device */
            ulong64     p_param;                /* buffer */
        } PACKED ioctl;                         /* IOCTL command structure */
        struct {
            ushort      reserved;
            union {
                struct {
                    ulong32  msg_handle;        /* message handle */
                    ulong64  msg_addr;          /* message buffer address */
                } PACKED msg;
                unchar       data[12];          /* buffer for rtc data, ... */
            } su;
        } PACKED screen;                        /* screen service cmd. str. */
        struct {
            ushort      reserved;
            ulong32     direction;              /* data direction */
            ulong32     mdisc_time;             /* disc. time (0: no timeout)*/
            ulong32     mcon_time;              /* connect time(0: no to.) */
            ulong32     sdata;                  /* dest. addr. (if s/g: -1) */
            ulong32     sdlen;                  /* data length (bytes) */
            ulong32     clen;                   /* SCSI cmd. length(6,10,12) */
            unchar      cmd[12];                /* SCSI command */
            unchar      target;                 /* target ID */
            unchar      lun;                    /* LUN */
            unchar      bus;                    /* SCSI bus number */
            unchar      priority;               /* only 0 used */
            ulong32     sense_len;              /* sense data length */
            ulong32     sense_data;             /* sense data addr. */
            ulong32     link_p;                 /* linked cmds (not supp.) */
            ulong32     sg_ranz;                /* s/g element count */
            gdth_sg_str sg_lst[GDTH_MAXSG];     /* s/g list */
        } PACKED raw;                           /* raw service cmd. struct. */
        struct {
            ushort      reserved;
            ulong32     direction;              /* data direction */
            ulong32     mdisc_time;             /* disc. time (0: no timeout)*/
            ulong32     mcon_time;              /* connect time(0: no to.) */
            ulong64     sdata;                  /* dest. addr. (if s/g: -1) */
            ulong32     sdlen;                  /* data length (bytes) */
            ulong32     clen;                   /* SCSI cmd. length(6,..,16) */
            unchar      cmd[16];                /* SCSI command */
            unchar      target;                 /* target ID */
            unchar      lun;                    /* LUN */
            unchar      bus;                    /* SCSI bus number */
            unchar      priority;               /* only 0 used */
            ulong32     sense_len;              /* sense data length */
            ulong64     sense_data;             /* sense data addr. */
            ulong32     sg_ranz;                /* s/g element count */
            gdth_sg64_str sg_lst[GDTH_MAXSG];   /* s/g list */
        } PACKED raw64;                         /* raw service cmd. struct. */
    } u;
    /* additional variables */
    unchar      Service;                        /* controller service */
    unchar      reserved;
    ushort      Status;                         /* command result */
    ulong32     Info;                           /* additional information */
    void        *RequestBuffer;                 /* request buffer */
} PACKED gdth_cmd_str;

/* controller event structure */
#define ES_ASYNC    1
#define ES_DRIVER   2
#define ES_TEST     3
#define ES_SYNC     4
typedef struct {
    ushort                  size;               /* size of structure */
    union {
        char                stream[16];
        struct {
            ushort          ionode;
            ushort          service;
            ulong32         index;
        } PACKED driver;
        struct {
            ushort          ionode;
            ushort          service;
            ushort          status;
            ulong32         info;
            unchar          scsi_coord[3];
        } PACKED async;
        struct {
            ushort          ionode;
            ushort          service;
            ushort          status;
            ulong32         info;
            ushort          hostdrive;
            unchar          scsi_coord[3];
            unchar          sense_key;
        } PACKED sync;
        struct {
            ulong32         l1, l2, l3, l4;
        } PACKED test;
    } eu;
    ulong32                 severity;
    unchar                  event_string[256];          
} PACKED gdth_evt_data;

typedef struct {
    ulong32         first_stamp;
    ulong32         last_stamp;
    ushort          same_count;
    ushort          event_source;
    ushort          event_idx;
    unchar          application;
    unchar          reserved;
    gdth_evt_data   event_data;
} PACKED gdth_evt_str;


#ifdef GDTH_IOCTL_PROC
/* IOCTL structure (write) */
typedef struct {
    ulong32                 magic;              /* IOCTL magic */
    ushort                  ioctl;              /* IOCTL */
    ushort                  ionode;             /* controller number */
    ushort                  service;            /* controller service */
    ushort                  timeout;            /* timeout */
    union {
        struct {
            unchar          command[512];       /* controller command */
            unchar          data[1];            /* add. data */
        } general;
        struct {
            unchar          lock;               /* lock/unlock */
            unchar          drive_cnt;          /* drive count */
            ushort          drives[MAX_HDRIVES];/* drives */
        } lockdrv;
        struct {
            unchar          lock;               /* lock/unlock */
            unchar          channel;            /* channel */
        } lockchn;
        struct {
            int             erase;              /* erase event ? */
            int             handle;
            unchar          evt[EVENT_SIZE];    /* event structure */
        } event;
        struct {
            unchar          bus;                /* SCSI bus */
            unchar          target;             /* target ID */
            unchar          lun;                /* LUN */
            unchar          cmd_len;            /* command length */
            unchar          cmd[12];            /* SCSI command */
        } scsi;
        struct {
            ushort          hdr_no;             /* host drive number */
            unchar          flag;               /* old meth./add/remove */
        } rescan;
    } iu;
} gdth_iowr_str;

/* IOCTL structure (read) */
typedef struct {
    ulong32                 size;               /* buffer size */
    ulong32                 status;             /* IOCTL error code */
    union {
        struct {
            unchar          data[1];            /* data */
        } general;
        struct {
            ushort          version;            /* driver version */
        } drvers;
        struct {
            unchar          type;               /* controller type */
            ushort          info;               /* slot etc. */
            ushort          oem_id;             /* OEM ID */
            ushort          bios_ver;           /* not used */
            ushort          access;             /* not used */
            ushort          ext_type;           /* extended type */
            ushort          device_id;          /* device ID */
            ushort          sub_device_id;      /* sub device ID */
        } ctrtype;
        struct {
            unchar          version;            /* OS version */
            unchar          subversion;         /* OS subversion */
            ushort          revision;           /* revision */
        } osvers;
        struct {
            ushort          count;              /* controller count */
        } ctrcnt;
        struct {
            int             handle;
            unchar          evt[EVENT_SIZE];    /* event structure */
        } event;
        struct {
            unchar          bus;                /* SCSI bus, 0xff: invalid */
            unchar          target;             /* target ID */
            unchar          lun;                /* LUN */
            unchar          cluster_type;       /* cluster properties */
        } hdr_list[MAX_HDRIVES];                /* index is host drive number */
    } iu;
} gdth_iord_str;
#endif

/* GDTIOCTL_GENERAL */
typedef struct {
    ushort ionode;                              /* controller number */
    ushort timeout;                             /* timeout */
    ulong32 info;                               /* error info */ 
    ushort status;                              /* status */
    ulong data_len;                             /* data buffer size */
    ulong sense_len;                            /* sense buffer size */
    gdth_cmd_str command;                       /* command */                   
} gdth_ioctl_general;

/* GDTIOCTL_LOCKDRV */
typedef struct {
    ushort ionode;                              /* controller number */
    unchar lock;                                /* lock/unlock */
    unchar drive_cnt;                           /* drive count */
    ushort drives[MAX_HDRIVES];                 /* drives */
} gdth_ioctl_lockdrv;

/* GDTIOCTL_LOCKCHN */
typedef struct {
    ushort ionode;                              /* controller number */
    unchar lock;                                /* lock/unlock */
    unchar channel;                             /* channel */
} gdth_ioctl_lockchn;

/* GDTIOCTL_OSVERS */
typedef struct {
    unchar version;                             /* OS version */
    unchar subversion;                          /* OS subversion */
    ushort revision;                            /* revision */
} gdth_ioctl_osvers;

/* GDTIOCTL_CTRTYPE */
typedef struct {
    ushort ionode;                              /* controller number */
    unchar type;                                /* controller type */
    ushort info;                                /* slot etc. */
    ushort oem_id;                              /* OEM ID */
    ushort bios_ver;                            /* not used */
    ushort access;                              /* not used */
    ushort ext_type;                            /* extended type */
    ushort device_id;                           /* device ID */
    ushort sub_device_id;                       /* sub device ID */
} gdth_ioctl_ctrtype;

/* GDTIOCTL_EVENT */
typedef struct {
    ushort ionode;
    int erase;                                  /* erase event? */
    int handle;                                 /* event handle */
    gdth_evt_str event;
} gdth_ioctl_event;

/* GDTIOCTL_RESCAN/GDTIOCTL_HDRLIST */
typedef struct {
    ushort ionode;                              /* controller number */
    unchar flag;                                /* add/remove */
    ushort hdr_no;                              /* drive no. */
    struct {
        unchar bus;                             /* SCSI bus */
        unchar target;                          /* target ID */
        unchar lun;                             /* LUN */
        unchar cluster_type;                    /* cluster properties */
    } hdr_list[MAX_HDRIVES];                    /* index is host drive number */
} gdth_ioctl_rescan;

/* GDTIOCTL_RESET_BUS/GDTIOCTL_RESET_DRV */
typedef struct {
    ushort ionode;                              /* controller number */
    ushort number;                              /* bus/host drive number */
    ushort status;                              /* status */
} gdth_ioctl_reset;

#endif
