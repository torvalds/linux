/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _GDTH_H
#define _GDTH_H

/*
 * Header file for the GDT Disk Array/Storage RAID controllers driver for Linux
 * 
 * gdth.h Copyright (C) 1995-06 ICP vortex, Achim Leubner
 * See gdth.c for further informations and 
 * below for supported controller types
 *
 * <achim_leubner@adaptec.com>
 *
 * $Id: gdth.h,v 1.58 2006/01/11 16:14:09 achim Exp $
 */

#include <linux/types.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* defines, macros */

/* driver version */
#define GDTH_VERSION_STR        "3.05"
#define GDTH_VERSION            3
#define GDTH_SUBVERSION         5

/* protocol version */
#define PROTOCOL_VERSION        1

/* OEM IDs */
#define OEM_ID_ICP      0x941c
#define OEM_ID_INTEL    0x8000

/* controller classes */
#define GDT_ISA         0x01                    /* ISA controller */
#define GDT_EISA        0x02                    /* EISA controller */
#define GDT_PCI         0x03                    /* PCI controller */
#define GDT_PCINEW      0x04                    /* new PCI controller */
#define GDT_PCIMPR      0x05                    /* PCI MPR controller */
/* GDT_EISA, controller subtypes EISA */
#define GDT3_ID         0x0130941c              /* GDT3000/3020 */
#define GDT3A_ID        0x0230941c              /* GDT3000A/3020A/3050A */
#define GDT3B_ID        0x0330941c              /* GDT3000B/3010A */
/* GDT_ISA */
#define GDT2_ID         0x0120941c              /* GDT2000/2020 */

#ifndef PCI_DEVICE_ID_VORTEX_GDT60x0
/* GDT_PCI */
#define PCI_DEVICE_ID_VORTEX_GDT60x0    0       /* GDT6000/6020/6050 */
#define PCI_DEVICE_ID_VORTEX_GDT6000B   1       /* GDT6000B/6010 */
/* GDT_PCINEW */
#define PCI_DEVICE_ID_VORTEX_GDT6x10    2       /* GDT6110/6510 */
#define PCI_DEVICE_ID_VORTEX_GDT6x20    3       /* GDT6120/6520 */
#define PCI_DEVICE_ID_VORTEX_GDT6530    4       /* GDT6530 */
#define PCI_DEVICE_ID_VORTEX_GDT6550    5       /* GDT6550 */
/* GDT_PCINEW, wide/ultra SCSI controllers */
#define PCI_DEVICE_ID_VORTEX_GDT6x17    6       /* GDT6117/6517 */
#define PCI_DEVICE_ID_VORTEX_GDT6x27    7       /* GDT6127/6527 */
#define PCI_DEVICE_ID_VORTEX_GDT6537    8       /* GDT6537 */
#define PCI_DEVICE_ID_VORTEX_GDT6557    9       /* GDT6557/6557-ECC */
/* GDT_PCINEW, wide SCSI controllers */
#define PCI_DEVICE_ID_VORTEX_GDT6x15    10      /* GDT6115/6515 */
#define PCI_DEVICE_ID_VORTEX_GDT6x25    11      /* GDT6125/6525 */
#define PCI_DEVICE_ID_VORTEX_GDT6535    12      /* GDT6535 */
#define PCI_DEVICE_ID_VORTEX_GDT6555    13      /* GDT6555/6555-ECC */
#endif

#ifndef PCI_DEVICE_ID_VORTEX_GDT6x17RP
/* GDT_MPR, RP series, wide/ultra SCSI */
#define PCI_DEVICE_ID_VORTEX_GDT6x17RP  0x100   /* GDT6117RP/GDT6517RP */
#define PCI_DEVICE_ID_VORTEX_GDT6x27RP  0x101   /* GDT6127RP/GDT6527RP */
#define PCI_DEVICE_ID_VORTEX_GDT6537RP  0x102   /* GDT6537RP */
#define PCI_DEVICE_ID_VORTEX_GDT6557RP  0x103   /* GDT6557RP */
/* GDT_MPR, RP series, narrow/ultra SCSI */
#define PCI_DEVICE_ID_VORTEX_GDT6x11RP  0x104   /* GDT6111RP/GDT6511RP */
#define PCI_DEVICE_ID_VORTEX_GDT6x21RP  0x105   /* GDT6121RP/GDT6521RP */
#endif
#ifndef PCI_DEVICE_ID_VORTEX_GDT6x17RD
/* GDT_MPR, RD series, wide/ultra SCSI */
#define PCI_DEVICE_ID_VORTEX_GDT6x17RD  0x110   /* GDT6117RD/GDT6517RD */
#define PCI_DEVICE_ID_VORTEX_GDT6x27RD  0x111   /* GDT6127RD/GDT6527RD */
#define PCI_DEVICE_ID_VORTEX_GDT6537RD  0x112   /* GDT6537RD */
#define PCI_DEVICE_ID_VORTEX_GDT6557RD  0x113   /* GDT6557RD */
/* GDT_MPR, RD series, narrow/ultra SCSI */
#define PCI_DEVICE_ID_VORTEX_GDT6x11RD  0x114   /* GDT6111RD/GDT6511RD */
#define PCI_DEVICE_ID_VORTEX_GDT6x21RD  0x115   /* GDT6121RD/GDT6521RD */
/* GDT_MPR, RD series, wide/ultra2 SCSI */
#define PCI_DEVICE_ID_VORTEX_GDT6x18RD  0x118   /* GDT6118RD/GDT6518RD/
                                                   GDT6618RD */
#define PCI_DEVICE_ID_VORTEX_GDT6x28RD  0x119   /* GDT6128RD/GDT6528RD/
                                                   GDT6628RD */
#define PCI_DEVICE_ID_VORTEX_GDT6x38RD  0x11A   /* GDT6538RD/GDT6638RD */
#define PCI_DEVICE_ID_VORTEX_GDT6x58RD  0x11B   /* GDT6558RD/GDT6658RD */
/* GDT_MPR, RN series (64-bit PCI), wide/ultra2 SCSI */
#define PCI_DEVICE_ID_VORTEX_GDT7x18RN  0x168   /* GDT7118RN/GDT7518RN/
                                                   GDT7618RN */
#define PCI_DEVICE_ID_VORTEX_GDT7x28RN  0x169   /* GDT7128RN/GDT7528RN/
                                                   GDT7628RN */
#define PCI_DEVICE_ID_VORTEX_GDT7x38RN  0x16A   /* GDT7538RN/GDT7638RN */
#define PCI_DEVICE_ID_VORTEX_GDT7x58RN  0x16B   /* GDT7558RN/GDT7658RN */
#endif

#ifndef PCI_DEVICE_ID_VORTEX_GDT6x19RD
/* GDT_MPR, RD series, Fibre Channel */
#define PCI_DEVICE_ID_VORTEX_GDT6x19RD  0x210   /* GDT6519RD/GDT6619RD */
#define PCI_DEVICE_ID_VORTEX_GDT6x29RD  0x211   /* GDT6529RD/GDT6629RD */
/* GDT_MPR, RN series (64-bit PCI), Fibre Channel */
#define PCI_DEVICE_ID_VORTEX_GDT7x19RN  0x260   /* GDT7519RN/GDT7619RN */
#define PCI_DEVICE_ID_VORTEX_GDT7x29RN  0x261   /* GDT7529RN/GDT7629RN */
#endif

#ifndef PCI_DEVICE_ID_VORTEX_GDTMAXRP
/* GDT_MPR, last device ID */
#define PCI_DEVICE_ID_VORTEX_GDTMAXRP   0x2ff   
#endif

#ifndef PCI_DEVICE_ID_VORTEX_GDTNEWRX
/* new GDT Rx Controller */
#define PCI_DEVICE_ID_VORTEX_GDTNEWRX   0x300
#endif

#ifndef PCI_DEVICE_ID_VORTEX_GDTNEWRX2
/* new(2) GDT Rx Controller */
#define PCI_DEVICE_ID_VORTEX_GDTNEWRX2  0x301
#endif        

#ifndef PCI_DEVICE_ID_INTEL_SRC
/* Intel Storage RAID Controller */
#define PCI_DEVICE_ID_INTEL_SRC         0x600
#endif

#ifndef PCI_DEVICE_ID_INTEL_SRC_XSCALE
/* Intel Storage RAID Controller */
#define PCI_DEVICE_ID_INTEL_SRC_XSCALE  0x601
#endif

/* limits */
#define GDTH_SCRATCH    PAGE_SIZE               /* 4KB scratch buffer */
#define GDTH_MAXCMDS    120
#define GDTH_MAXC_P_L   16                      /* max. cmds per lun */
#define GDTH_MAX_RAW    2                       /* max. cmds per raw device */
#define MAXOFFSETS      128
#define MAXHA           16
#define MAXID           127
#define MAXLUN          8
#define MAXBUS          6
#define MAX_EVENTS      100                     /* event buffer count */
#define MAX_RES_ARGS    40                      /* device reservation, 
                                                   must be a multiple of 4 */
#define MAXCYLS         1024
#define HEADS           64
#define SECS            32                      /* mapping 64*32 */
#define MEDHEADS        127
#define MEDSECS         63                      /* mapping 127*63 */
#define BIGHEADS        255
#define BIGSECS         63                      /* mapping 255*63 */

/* special command ptr. */
#define UNUSED_CMND     ((struct scsi_cmnd *)-1)
#define INTERNAL_CMND   ((struct scsi_cmnd *)-2)
#define SCREEN_CMND     ((struct scsi_cmnd *)-3)
#define SPECIAL_SCP(p)  (p==UNUSED_CMND || p==INTERNAL_CMND || p==SCREEN_CMND)

/* controller services */
#define SCSIRAWSERVICE  3
#define CACHESERVICE    9
#define SCREENSERVICE   11

/* screenservice defines */
#define MSG_INV_HANDLE  -1                      /* special message handle */
#define MSGLEN          16                      /* size of message text */
#define MSG_SIZE        34                      /* size of message structure */
#define MSG_REQUEST     0                       /* async. event: message */

/* DPMEM constants */
#define DPMEM_MAGIC     0xC0FFEE11
#define IC_HEADER_BYTES 48
#define IC_QUEUE_BYTES  4
#define DPMEM_COMMAND_OFFSET    IC_HEADER_BYTES+IC_QUEUE_BYTES*MAXOFFSETS

/* cluster_type constants */
#define CLUSTER_DRIVE         1
#define CLUSTER_MOUNTED       2
#define CLUSTER_RESERVED      4
#define CLUSTER_RESERVE_STATE (CLUSTER_DRIVE|CLUSTER_MOUNTED|CLUSTER_RESERVED)

/* commands for all services, cache service */
#define GDT_INIT        0                       /* service initialization */
#define GDT_READ        1                       /* read command */
#define GDT_WRITE       2                       /* write command */
#define GDT_INFO        3                       /* information about devices */
#define GDT_FLUSH       4                       /* flush dirty cache buffers */
#define GDT_IOCTL       5                       /* ioctl command */
#define GDT_DEVTYPE     9                       /* additional information */
#define GDT_MOUNT       10                      /* mount cache device */
#define GDT_UNMOUNT     11                      /* unmount cache device */
#define GDT_SET_FEAT    12                      /* set feat. (scatter/gather) */
#define GDT_GET_FEAT    13                      /* get features */
#define GDT_WRITE_THR   16                      /* write through */
#define GDT_READ_THR    17                      /* read through */
#define GDT_EXT_INFO    18                      /* extended info */
#define GDT_RESET       19                      /* controller reset */
#define GDT_RESERVE_DRV 20                      /* reserve host drive */
#define GDT_RELEASE_DRV 21                      /* release host drive */
#define GDT_CLUST_INFO  22                      /* cluster info */
#define GDT_RW_ATTRIBS  23                      /* R/W attribs (write thru,..)*/
#define GDT_CLUST_RESET 24                      /* releases the cluster drives*/
#define GDT_FREEZE_IO   25                      /* freezes all IOs */
#define GDT_UNFREEZE_IO 26                      /* unfreezes all IOs */
#define GDT_X_INIT_HOST 29                      /* ext. init: 64 bit support */
#define GDT_X_INFO      30                      /* ext. info for drives>2TB */

/* raw service commands */
#define GDT_RESERVE     14                      /* reserve dev. to raw serv. */
#define GDT_RELEASE     15                      /* release device */
#define GDT_RESERVE_ALL 16                      /* reserve all devices */
#define GDT_RELEASE_ALL 17                      /* release all devices */
#define GDT_RESET_BUS   18                      /* reset bus */
#define GDT_SCAN_START  19                      /* start device scan */
#define GDT_SCAN_END    20                      /* stop device scan */  
#define GDT_X_INIT_RAW  21                      /* ext. init: 64 bit support */

/* screen service commands */
#define GDT_REALTIME    3                       /* realtime clock to screens. */
#define GDT_X_INIT_SCR  4                       /* ext. init: 64 bit support */

/* IOCTL command defines */
#define SCSI_DR_INFO    0x00                    /* SCSI drive info */                   
#define SCSI_CHAN_CNT   0x05                    /* SCSI channel count */   
#define SCSI_DR_LIST    0x06                    /* SCSI drive list */
#define SCSI_DEF_CNT    0x15                    /* grown/primary defects */
#define DSK_STATISTICS  0x4b                    /* SCSI disk statistics */
#define IOCHAN_DESC     0x5d                    /* description of IO channel */
#define IOCHAN_RAW_DESC 0x5e                    /* description of raw IO chn. */
#define L_CTRL_PATTERN  0x20000000L             /* SCSI IOCTL mask */
#define ARRAY_INFO      0x12                    /* array drive info */
#define ARRAY_DRV_LIST  0x0f                    /* array drive list */
#define ARRAY_DRV_LIST2 0x34                    /* array drive list (new) */
#define LA_CTRL_PATTERN 0x10000000L             /* array IOCTL mask */
#define CACHE_DRV_CNT   0x01                    /* cache drive count */
#define CACHE_DRV_LIST  0x02                    /* cache drive list */
#define CACHE_INFO      0x04                    /* cache info */
#define CACHE_CONFIG    0x05                    /* cache configuration */
#define CACHE_DRV_INFO  0x07                    /* cache drive info */
#define BOARD_FEATURES  0x15                    /* controller features */
#define BOARD_INFO      0x28                    /* controller info */
#define SET_PERF_MODES  0x82                    /* set mode (coalescing,..) */
#define GET_PERF_MODES  0x83                    /* get mode */
#define CACHE_READ_OEM_STRING_RECORD 0x84       /* read OEM string record */ 
#define HOST_GET        0x10001L                /* get host drive list */
#define IO_CHANNEL      0x00020000L             /* default IO channel */
#define INVALID_CHANNEL 0x0000ffffL             /* invalid channel */

/* service errors */
#define S_OK            1                       /* no error */
#define S_GENERR        6                       /* general error */
#define S_BSY           7                       /* controller busy */
#define S_CACHE_UNKNOWN 12                      /* cache serv.: drive unknown */
#define S_RAW_SCSI      12                      /* raw serv.: target error */
#define S_RAW_ILL       0xff                    /* raw serv.: illegal */
#define S_NOFUNC        -2                      /* unknown function */
#define S_CACHE_RESERV  -24                     /* cache: reserv. conflict */   

/* timeout values */
#define INIT_RETRIES    100000                  /* 100000 * 1ms = 100s */
#define INIT_TIMEOUT    100000                  /* 100000 * 1ms = 100s */
#define POLL_TIMEOUT    10000                   /* 10000 * 1ms = 10s */

/* priorities */
#define DEFAULT_PRI     0x20
#define IOCTL_PRI       0x10
#define HIGH_PRI        0x08

/* data directions */
#define GDTH_DATA_IN    0x01000000L             /* data from target */
#define GDTH_DATA_OUT   0x00000000L             /* data to target */

/* BMIC registers (EISA controllers) */
#define ID0REG          0x0c80                  /* board ID */
#define EINTENABREG     0x0c89                  /* interrupt enable */
#define SEMA0REG        0x0c8a                  /* command semaphore */
#define SEMA1REG        0x0c8b                  /* status semaphore */
#define LDOORREG        0x0c8d                  /* local doorbell */
#define EDENABREG       0x0c8e                  /* EISA system doorbell enab. */
#define EDOORREG        0x0c8f                  /* EISA system doorbell */
#define MAILBOXREG      0x0c90                  /* mailbox reg. (16 bytes) */
#define EISAREG         0x0cc0                  /* EISA configuration */

/* other defines */
#define LINUX_OS        8                       /* used for cache optim. */
#define SECS32          0x1f                    /* round capacity */
#define BIOS_ID_OFFS    0x10                    /* offset contr-ID in ISABIOS */
#define LOCALBOARD      0                       /* board node always 0 */
#define ASYNCINDEX      0                       /* cmd index async. event */
#define SPEZINDEX       1                       /* cmd index unknown service */
#define COALINDEX       (GDTH_MAXCMDS + 2)

/* features */
#define SCATTER_GATHER  1                       /* s/g feature */
#define GDT_WR_THROUGH  0x100                   /* WRITE_THROUGH supported */
#define GDT_64BIT       0x200                   /* 64bit / drv>2TB support */

#include "gdth_ioctl.h"

/* screenservice message */
typedef struct {                               
    u32     msg_handle;                     /* message handle */
    u32     msg_len;                        /* size of message */
    u32     msg_alen;                       /* answer length */
    u8      msg_answer;                     /* answer flag */
    u8      msg_ext;                        /* more messages */
    u8      msg_reserved[2];
    char        msg_text[MSGLEN+2];             /* the message text */
} __attribute__((packed)) gdth_msg_str;


/* IOCTL data structures */

/* Status coalescing buffer for returning multiple requests per interrupt */
typedef struct {
    u32     status;
    u32     ext_status;
    u32     info0;
    u32     info1;
} __attribute__((packed)) gdth_coal_status;

/* performance mode data structure */
typedef struct {
    u32     version;            /* The version of this IOCTL structure. */
    u32     st_mode;            /* 0=dis., 1=st_buf_addr1 valid, 2=both  */
    u32     st_buff_addr1;      /* physical address of status buffer 1 */
    u32     st_buff_u_addr1;    /* reserved for 64 bit addressing */
    u32     st_buff_indx1;      /* reserved command idx. for this buffer */
    u32     st_buff_addr2;      /* physical address of status buffer 1 */
    u32     st_buff_u_addr2;    /* reserved for 64 bit addressing */
    u32     st_buff_indx2;      /* reserved command idx. for this buffer */
    u32     st_buff_size;       /* size of each buffer in bytes */
    u32     cmd_mode;           /* 0 = mode disabled, 1 = cmd_buff_addr1 */ 
    u32     cmd_buff_addr1;     /* physical address of cmd buffer 1 */   
    u32     cmd_buff_u_addr1;   /* reserved for 64 bit addressing */
    u32     cmd_buff_indx1;     /* cmd buf addr1 unique identifier */
    u32     cmd_buff_addr2;     /* physical address of cmd buffer 1 */   
    u32     cmd_buff_u_addr2;   /* reserved for 64 bit addressing */
    u32     cmd_buff_indx2;     /* cmd buf addr1 unique identifier */
    u32     cmd_buff_size;      /* size of each cmd buffer in bytes */
    u32     reserved1;
    u32     reserved2;
} __attribute__((packed)) gdth_perf_modes;

/* SCSI drive info */
typedef struct {
    u8      vendor[8];                      /* vendor string */
    u8      product[16];                    /* product string */
    u8      revision[4];                    /* revision */
    u32     sy_rate;                        /* current rate for sync. tr. */
    u32     sy_max_rate;                    /* max. rate for sync. tr. */
    u32     no_ldrive;                      /* belongs to this log. drv.*/
    u32     blkcnt;                         /* number of blocks */
    u16      blksize;                        /* size of block in bytes */
    u8      available;                      /* flag: access is available */
    u8      init;                           /* medium is initialized */
    u8      devtype;                        /* SCSI devicetype */
    u8      rm_medium;                      /* medium is removable */
    u8      wp_medium;                      /* medium is write protected */
    u8      ansi;                           /* SCSI I/II or III? */
    u8      protocol;                       /* same as ansi */
    u8      sync;                           /* flag: sync. transfer enab. */
    u8      disc;                           /* flag: disconnect enabled */
    u8      queueing;                       /* flag: command queing enab. */
    u8      cached;                         /* flag: caching enabled */
    u8      target_id;                      /* target ID of device */
    u8      lun;                            /* LUN id of device */
    u8      orphan;                         /* flag: drive fragment */
    u32     last_error;                     /* sense key or drive state */
    u32     last_result;                    /* result of last command */
    u32     check_errors;                   /* err. in last surface check */
    u8      percent;                        /* progress for surface check */
    u8      last_check;                     /* IOCTRL operation */
    u8      res[2];
    u32     flags;                          /* from 1.19/2.19: raw reserv.*/
    u8      multi_bus;                      /* multi bus dev? (fibre ch.) */
    u8      mb_status;                      /* status: available? */
    u8      res2[2];
    u8      mb_alt_status;                  /* status on second bus */
    u8      mb_alt_bid;                     /* number of second bus */
    u8      mb_alt_tid;                     /* target id on second bus */
    u8      res3;
    u8      fc_flag;                        /* from 1.22/2.22: info valid?*/
    u8      res4;
    u16      fc_frame_size;                  /* frame size (bytes) */
    char        wwn[8];                         /* world wide name */
} __attribute__((packed)) gdth_diskinfo_str;

/* get SCSI channel count  */
typedef struct {
    u32     channel_no;                     /* number of channel */
    u32     drive_cnt;                      /* drive count */
    u8      siop_id;                        /* SCSI processor ID */
    u8      siop_state;                     /* SCSI processor state */ 
} __attribute__((packed)) gdth_getch_str;

/* get SCSI drive numbers */
typedef struct {
    u32     sc_no;                          /* SCSI channel */
    u32     sc_cnt;                         /* sc_list[] elements */
    u32     sc_list[MAXID];                 /* minor device numbers */
} __attribute__((packed)) gdth_drlist_str;

/* get grown/primary defect count */
typedef struct {
    u8      sddc_type;                      /* 0x08: grown, 0x10: prim. */
    u8      sddc_format;                    /* list entry format */
    u8      sddc_len;                       /* list entry length */
    u8      sddc_res;
    u32     sddc_cnt;                       /* entry count */
} __attribute__((packed)) gdth_defcnt_str;

/* disk statistics */
typedef struct {
    u32     bid;                            /* SCSI channel */
    u32     first;                          /* first SCSI disk */
    u32     entries;                        /* number of elements */
    u32     count;                          /* (R) number of init. el. */
    u32     mon_time;                       /* time stamp */
    struct {
        u8  tid;                            /* target ID */
        u8  lun;                            /* LUN */
        u8  res[2];
        u32 blk_size;                       /* block size in bytes */
        u32 rd_count;                       /* bytes read */
        u32 wr_count;                       /* bytes written */
        u32 rd_blk_count;                   /* blocks read */
        u32 wr_blk_count;                   /* blocks written */
        u32 retries;                        /* retries */
        u32 reassigns;                      /* reassigns */
    } __attribute__((packed)) list[1];
} __attribute__((packed)) gdth_dskstat_str;

/* IO channel header */
typedef struct {
    u32     version;                        /* version (-1UL: newest) */
    u8      list_entries;                   /* list entry count */
    u8      first_chan;                     /* first channel number */
    u8      last_chan;                      /* last channel number */
    u8      chan_count;                     /* (R) channel count */
    u32     list_offset;                    /* offset of list[0] */
} __attribute__((packed)) gdth_iochan_header;

/* get IO channel description */
typedef struct {
    gdth_iochan_header  hdr;
    struct {
        u32         address;                /* channel address */
        u8          type;                   /* type (SCSI, FCAL) */
        u8          local_no;               /* local number */
        u16          features;               /* channel features */
    } __attribute__((packed)) list[MAXBUS];
} __attribute__((packed)) gdth_iochan_str;

/* get raw IO channel description */
typedef struct {
    gdth_iochan_header  hdr;
    struct {
        u8      proc_id;                    /* processor id */
        u8      proc_defect;                /* defect ? */
        u8      reserved[2];
    } __attribute__((packed)) list[MAXBUS];
} __attribute__((packed)) gdth_raw_iochan_str;

/* array drive component */
typedef struct {
    u32     al_controller;                  /* controller ID */
    u8      al_cache_drive;                 /* cache drive number */
    u8      al_status;                      /* cache drive state */
    u8      al_res[2];     
} __attribute__((packed)) gdth_arraycomp_str;

/* array drive information */
typedef struct {
    u8      ai_type;                        /* array type (RAID0,4,5) */
    u8      ai_cache_drive_cnt;             /* active cachedrives */
    u8      ai_state;                       /* array drive state */
    u8      ai_master_cd;                   /* master cachedrive */
    u32     ai_master_controller;           /* ID of master controller */
    u32     ai_size;                        /* user capacity [sectors] */
    u32     ai_striping_size;               /* striping size [sectors] */
    u32     ai_secsize;                     /* sector size [bytes] */
    u32     ai_err_info;                    /* failed cache drive */
    u8      ai_name[8];                     /* name of the array drive */
    u8      ai_controller_cnt;              /* number of controllers */
    u8      ai_removable;                   /* flag: removable */
    u8      ai_write_protected;             /* flag: write protected */
    u8      ai_devtype;                     /* type: always direct access */
    gdth_arraycomp_str  ai_drives[35];          /* drive components: */
    u8      ai_drive_entries;               /* number of drive components */
    u8      ai_protected;                   /* protection flag */
    u8      ai_verify_state;                /* state of a parity verify */
    u8      ai_ext_state;                   /* extended array drive state */
    u8      ai_expand_state;                /* array expand state (>=2.18)*/
    u8      ai_reserved[3];
} __attribute__((packed)) gdth_arrayinf_str;

/* get array drive list */
typedef struct {
    u32     controller_no;                  /* controller no. */
    u8      cd_handle;                      /* master cachedrive */
    u8      is_arrayd;                      /* Flag: is array drive? */
    u8      is_master;                      /* Flag: is array master? */
    u8      is_parity;                      /* Flag: is parity drive? */
    u8      is_hotfix;                      /* Flag: is hotfix drive? */
    u8      res[3];
} __attribute__((packed)) gdth_alist_str;

typedef struct {
    u32     entries_avail;                  /* allocated entries */
    u32     entries_init;                   /* returned entries */
    u32     first_entry;                    /* first entry number */
    u32     list_offset;                    /* offset of following list */
    gdth_alist_str list[1];                     /* list */
} __attribute__((packed)) gdth_arcdl_str;

/* cache info/config IOCTL */
typedef struct {
    u32     version;                        /* firmware version */
    u16      state;                          /* cache state (on/off) */
    u16      strategy;                       /* cache strategy */
    u16      write_back;                     /* write back state (on/off) */
    u16      block_size;                     /* cache block size */
} __attribute__((packed)) gdth_cpar_str;

typedef struct {
    u32     csize;                          /* cache size */
    u32     read_cnt;                       /* read/write counter */
    u32     write_cnt;
    u32     tr_hits;                        /* hits */
    u32     sec_hits;
    u32     sec_miss;                       /* misses */
} __attribute__((packed)) gdth_cstat_str;

typedef struct {
    gdth_cpar_str   cpar;
    gdth_cstat_str  cstat;
} __attribute__((packed)) gdth_cinfo_str;

/* cache drive info */
typedef struct {
    u8      cd_name[8];                     /* cache drive name */
    u32     cd_devtype;                     /* SCSI devicetype */
    u32     cd_ldcnt;                       /* number of log. drives */
    u32     cd_last_error;                  /* last error */
    u8      cd_initialized;                 /* drive is initialized */
    u8      cd_removable;                   /* media is removable */
    u8      cd_write_protected;             /* write protected */
    u8      cd_flags;                       /* Pool Hot Fix? */
    u32     ld_blkcnt;                      /* number of blocks */
    u32     ld_blksize;                     /* blocksize */
    u32     ld_dcnt;                        /* number of disks */
    u32     ld_slave;                       /* log. drive index */
    u32     ld_dtype;                       /* type of logical drive */
    u32     ld_last_error;                  /* last error */
    u8      ld_name[8];                     /* log. drive name */
    u8      ld_error;                       /* error */
} __attribute__((packed)) gdth_cdrinfo_str;

/* OEM string */
typedef struct {
    u32     ctl_version;
    u32     file_major_version;
    u32     file_minor_version;
    u32     buffer_size;
    u32     cpy_count;
    u32     ext_error;
    u32     oem_id;
    u32     board_id;
} __attribute__((packed)) gdth_oem_str_params;

typedef struct {
    u8      product_0_1_name[16];
    u8      product_4_5_name[16];
    u8      product_cluster_name[16];
    u8      product_reserved[16];
    u8      scsi_cluster_target_vendor_id[16];
    u8      cluster_raid_fw_name[16];
    u8      oem_brand_name[16];
    u8      oem_raid_type[16];
    u8      bios_type[13];
    u8      bios_title[50];
    u8      oem_company_name[37];
    u32     pci_id_1;
    u32     pci_id_2;
    u8      validation_status[80];
    u8      reserved_1[4];
    u8      scsi_host_drive_inquiry_vendor_id[16];
    u8      library_file_template[16];
    u8      reserved_2[16];
    u8      tool_name_1[32];
    u8      tool_name_2[32];
    u8      tool_name_3[32];
    u8      oem_contact_1[84];
    u8      oem_contact_2[84];
    u8      oem_contact_3[84];
} __attribute__((packed)) gdth_oem_str;

typedef struct {
    gdth_oem_str_params params;
    gdth_oem_str        text;
} __attribute__((packed)) gdth_oem_str_ioctl;

/* board features */
typedef struct {
    u8      chaining;                       /* Chaining supported */
    u8      striping;                       /* Striping (RAID-0) supp. */
    u8      mirroring;                      /* Mirroring (RAID-1) supp. */
    u8      raid;                           /* RAID-4/5/10 supported */
} __attribute__((packed)) gdth_bfeat_str;

/* board info IOCTL */
typedef struct {
    u32     ser_no;                         /* serial no. */
    u8      oem_id[2];                      /* OEM ID */
    u16      ep_flags;                       /* eprom flags */
    u32     proc_id;                        /* processor ID */
    u32     memsize;                        /* memory size (bytes) */
    u8      mem_banks;                      /* memory banks */
    u8      chan_type;                      /* channel type */
    u8      chan_count;                     /* channel count */
    u8      rdongle_pres;                   /* dongle present? */
    u32     epr_fw_ver;                     /* (eprom) firmware version */
    u32     upd_fw_ver;                     /* (update) firmware version */
    u32     upd_revision;                   /* update revision */
    char        type_string[16];                /* controller name */
    char        raid_string[16];                /* RAID firmware name */
    u8      update_pres;                    /* update present? */
    u8      xor_pres;                       /* XOR engine present? */
    u8      prom_type;                      /* ROM type (eprom/flash) */
    u8      prom_count;                     /* number of ROM devices */
    u32     dup_pres;                       /* duplexing module present? */
    u32     chan_pres;                      /* number of expansion chn. */
    u32     mem_pres;                       /* memory expansion inst. ? */
    u8      ft_bus_system;                  /* fault bus supported? */
    u8      subtype_valid;                  /* board_subtype valid? */
    u8      board_subtype;                  /* subtype/hardware level */
    u8      ramparity_pres;                 /* RAM parity check hardware? */
} __attribute__((packed)) gdth_binfo_str; 

/* get host drive info */
typedef struct {
    char        name[8];                        /* host drive name */
    u32     size;                           /* size (sectors) */
    u8      host_drive;                     /* host drive number */
    u8      log_drive;                      /* log. drive (master) */
    u8      reserved;
    u8      rw_attribs;                     /* r/w attribs */
    u32     start_sec;                      /* start sector */
} __attribute__((packed)) gdth_hentry_str;

typedef struct {
    u32     entries;                        /* entry count */
    u32     offset;                         /* offset of entries */
    u8      secs_p_head;                    /* sectors/head */
    u8      heads_p_cyl;                    /* heads/cylinder */
    u8      reserved;
    u8      clust_drvtype;                  /* cluster drive type */
    u32     location;                       /* controller number */
    gdth_hentry_str entry[MAX_HDRIVES];         /* entries */
} __attribute__((packed)) gdth_hget_str;    


/* DPRAM structures */

/* interface area ISA/PCI */
typedef struct {
    u8              S_Cmd_Indx;             /* special command */
    u8 volatile     S_Status;               /* status special command */
    u16              reserved1;
    u32             S_Info[4];              /* add. info special command */
    u8 volatile     Sema0;                  /* command semaphore */
    u8              reserved2[3];
    u8              Cmd_Index;              /* command number */
    u8              reserved3[3];
    u16 volatile     Status;                 /* command status */
    u16              Service;                /* service(for async.events) */
    u32             Info[2];                /* additional info */
    struct {
        u16          offset;                 /* command offs. in the DPRAM*/
        u16          serv_id;                /* service */
    } __attribute__((packed)) comm_queue[MAXOFFSETS];            /* command queue */
    u32             bios_reserved[2];
    u8              gdt_dpr_cmd[1];         /* commands */
} __attribute__((packed)) gdt_dpr_if;

/* SRAM structure PCI controllers */
typedef struct {
    u32     magic;                          /* controller ID from BIOS */
    u16      need_deinit;                    /* switch betw. BIOS/driver */
    u8      switch_support;                 /* see need_deinit */
    u8      padding[9];
    u8      os_used[16];                    /* OS code per service */
    u8      unused[28];
    u8      fw_magic;                       /* contr. ID from firmware */
} __attribute__((packed)) gdt_pci_sram;

/* SRAM structure EISA controllers (but NOT GDT3000/3020) */
typedef struct {
    u8      os_used[16];                    /* OS code per service */
    u16      need_deinit;                    /* switch betw. BIOS/driver */
    u8      switch_support;                 /* see need_deinit */
    u8      padding;
} __attribute__((packed)) gdt_eisa_sram;


/* DPRAM ISA controllers */
typedef struct {
    union {
        struct {
            u8      bios_used[0x3c00-32];   /* 15KB - 32Bytes BIOS */
            u32     magic;                  /* controller (EISA) ID */
            u16      need_deinit;            /* switch betw. BIOS/driver */
            u8      switch_support;         /* see need_deinit */
            u8      padding[9];
            u8      os_used[16];            /* OS code per service */
        } __attribute__((packed)) dp_sram;
        u8          bios_area[0x4000];      /* 16KB reserved for BIOS */
    } bu;
    union {
        gdt_dpr_if      ic;                     /* interface area */
        u8          if_area[0x3000];        /* 12KB for interface */
    } u;
    struct {
        u8          memlock;                /* write protection DPRAM */
        u8          event;                  /* release event */
        u8          irqen;                  /* board interrupts enable */
        u8          irqdel;                 /* acknowledge board int. */
        u8 volatile Sema1;                  /* status semaphore */
        u8          rq;                     /* IRQ/DRQ configuration */
    } __attribute__((packed)) io;
} __attribute__((packed)) gdt2_dpram_str;

/* DPRAM PCI controllers */
typedef struct {
    union {
        gdt_dpr_if      ic;                     /* interface area */
        u8          if_area[0xff0-sizeof(gdt_pci_sram)];
    } u;
    gdt_pci_sram        gdt6sr;                 /* SRAM structure */
    struct {
        u8          unused0[1];
        u8 volatile Sema1;                  /* command semaphore */
        u8          unused1[3];
        u8          irqen;                  /* board interrupts enable */
        u8          unused2[2];
        u8          event;                  /* release event */
        u8          unused3[3];
        u8          irqdel;                 /* acknowledge board int. */
        u8          unused4[3];
    } __attribute__((packed)) io;
} __attribute__((packed)) gdt6_dpram_str;

/* PLX register structure (new PCI controllers) */
typedef struct {
    u8              cfg_reg;        /* DPRAM cfg.(2:below 1MB,0:anywhere)*/
    u8              unused1[0x3f];
    u8 volatile     sema0_reg;              /* command semaphore */
    u8 volatile     sema1_reg;              /* status semaphore */
    u8              unused2[2];
    u16 volatile     status;                 /* command status */
    u16              service;                /* service */
    u32             info[2];                /* additional info */
    u8              unused3[0x10];
    u8              ldoor_reg;              /* PCI to local doorbell */
    u8              unused4[3];
    u8 volatile     edoor_reg;              /* local to PCI doorbell */
    u8              unused5[3];
    u8              control0;               /* control0 register(unused) */
    u8              control1;               /* board interrupts enable */
    u8              unused6[0x16];
} __attribute__((packed)) gdt6c_plx_regs;

/* DPRAM new PCI controllers */
typedef struct {
    union {
        gdt_dpr_if      ic;                     /* interface area */
        u8          if_area[0x4000-sizeof(gdt_pci_sram)];
    } u;
    gdt_pci_sram        gdt6sr;                 /* SRAM structure */
} __attribute__((packed)) gdt6c_dpram_str;

/* i960 register structure (PCI MPR controllers) */
typedef struct {
    u8              unused1[16];
    u8 volatile     sema0_reg;              /* command semaphore */
    u8              unused2;
    u8 volatile     sema1_reg;              /* status semaphore */
    u8              unused3;
    u16 volatile     status;                 /* command status */
    u16              service;                /* service */
    u32             info[2];                /* additional info */
    u8              ldoor_reg;              /* PCI to local doorbell */
    u8              unused4[11];
    u8 volatile     edoor_reg;              /* local to PCI doorbell */
    u8              unused5[7];
    u8              edoor_en_reg;           /* board interrupts enable */
    u8              unused6[27];
    u32             unused7[939];         
    u32             severity;       
    char                evt_str[256];           /* event string */
} __attribute__((packed)) gdt6m_i960_regs;

/* DPRAM PCI MPR controllers */
typedef struct {
    gdt6m_i960_regs     i960r;                  /* 4KB i960 registers */
    union {
        gdt_dpr_if      ic;                     /* interface area */
        u8          if_area[0x3000-sizeof(gdt_pci_sram)];
    } u;
    gdt_pci_sram        gdt6sr;                 /* SRAM structure */
} __attribute__((packed)) gdt6m_dpram_str;


/* PCI resources */
typedef struct {
    struct pci_dev      *pdev;
    unsigned long               dpmem;                  /* DPRAM address */
    unsigned long               io;                     /* IO address */
} gdth_pci_str;


/* controller information structure */
typedef struct {
    struct Scsi_Host    *shost;
    struct list_head    list;
    u16      	hanum;
    u16              oem_id;                 /* OEM */
    u16              type;                   /* controller class */
    u32             stype;                  /* subtype (PCI: device ID) */
    u16              fw_vers;                /* firmware version */
    u16              cache_feat;             /* feat. cache serv. (s/g,..)*/
    u16              raw_feat;               /* feat. raw service (s/g,..)*/
    u16              screen_feat;            /* feat. raw service (s/g,..)*/
    u16              bmic;                   /* BMIC address (EISA) */
    void __iomem        *brd;                   /* DPRAM address */
    u32             brd_phys;               /* slot number/BIOS address */
    gdt6c_plx_regs      *plx;                   /* PLX regs (new PCI contr.) */
    gdth_cmd_str        cmdext;
    gdth_cmd_str        *pccb;                  /* address command structure */
    u32             ccb_phys;               /* phys. address */
#ifdef INT_COAL
    gdth_coal_status    *coal_stat;             /* buffer for coalescing int.*/
    u64             coal_stat_phys;         /* phys. address */
#endif
    char                *pscratch;              /* scratch (DMA) buffer */
    u64             scratch_phys;           /* phys. address */
    u8              scratch_busy;           /* in use? */
    u8              dma64_support;          /* 64-bit DMA supported? */
    gdth_msg_str        *pmsg;                  /* message buffer */
    u64             msg_phys;               /* phys. address */
    u8              scan_mode;              /* current scan mode */
    u8              irq;                    /* IRQ */
    u8              drq;                    /* DRQ (ISA controllers) */
    u16              status;                 /* command status */
    u16              service;                /* service/firmware ver./.. */
    u32             info;
    u32             info2;                  /* additional info */
    struct scsi_cmnd           *req_first;             /* top of request queue */
    struct {
        u8          present;                /* Flag: host drive present? */
        u8          is_logdrv;              /* Flag: log. drive (master)? */
        u8          is_arraydrv;            /* Flag: array drive? */
        u8          is_master;              /* Flag: array drive master? */
        u8          is_parity;              /* Flag: parity drive? */
        u8          is_hotfix;              /* Flag: hotfix drive? */
        u8          master_no;              /* number of master drive */
        u8          lock;                   /* drive locked? (hot plug) */
        u8          heads;                  /* mapping */
        u8          secs;
        u16          devtype;                /* further information */
        u64         size;                   /* capacity */
        u8          ldr_no;                 /* log. drive no. */
        u8          rw_attribs;             /* r/w attributes */
        u8          cluster_type;           /* cluster properties */
        u8          media_changed;          /* Flag:MOUNT/UNMOUNT occurred */
        u32         start_sec;              /* start sector */
    } hdr[MAX_LDRIVES];                         /* host drives */
    struct {
        u8          lock;                   /* channel locked? (hot plug) */
        u8          pdev_cnt;               /* physical device count */
        u8          local_no;               /* local channel number */
        u8          io_cnt[MAXID];          /* current IO count */
        u32         address;                /* channel address */
        u32         id_list[MAXID];         /* IDs of the phys. devices */
    } raw[MAXBUS];                              /* SCSI channels */
    struct {
        struct scsi_cmnd       *cmnd;                  /* pending request */
        u16          service;                /* service */
    } cmd_tab[GDTH_MAXCMDS];                    /* table of pend. requests */
    struct gdth_cmndinfo {                      /* per-command private info */
        int index;
        int internal_command;                   /* don't call scsi_done */
        gdth_cmd_str *internal_cmd_str;         /* crier for internal messages*/
        dma_addr_t sense_paddr;                 /* sense dma-addr */
        u8 priority;
	int timeout_count;			/* # of timeout calls */
        volatile int wait_for_completion;
        u16 status;
        u32 info;
        enum dma_data_direction dma_dir;
        int phase;                              /* ???? */
        int OpCode;
    } cmndinfo[GDTH_MAXCMDS];                   /* index==0 is free */
    u8              bus_cnt;                /* SCSI bus count */
    u8              tid_cnt;                /* Target ID count */
    u8              bus_id[MAXBUS];         /* IOP IDs */
    u8              virt_bus;               /* number of virtual bus */
    u8              more_proc;              /* more /proc info supported */
    u16              cmd_cnt;                /* command count in DPRAM */
    u16              cmd_len;                /* length of actual command */
    u16              cmd_offs_dpmem;         /* actual offset in DPRAM */
    u16              ic_all_size;            /* sizeof DPRAM interf. area */
    gdth_cpar_str       cpar;                   /* controller cache par. */
    gdth_bfeat_str      bfeat;                  /* controller features */
    gdth_binfo_str      binfo;                  /* controller info */
    gdth_evt_data       dvr;                    /* event structure */
    spinlock_t          smp_lock;
    struct pci_dev      *pdev;
    char                oem_name[8];
#ifdef GDTH_DMA_STATISTICS
    unsigned long               dma32_cnt, dma64_cnt;   /* statistics: DMA buffer */
#endif
    struct scsi_device         *sdev;
} gdth_ha_str;

static inline struct gdth_cmndinfo *gdth_cmnd_priv(struct scsi_cmnd* cmd)
{
	return (struct gdth_cmndinfo *)cmd->host_scribble;
}

/* INQUIRY data format */
typedef struct {
    u8      type_qual;
    u8      modif_rmb;
    u8      version;
    u8      resp_aenc;
    u8      add_length;
    u8      reserved1;
    u8      reserved2;
    u8      misc;
    u8      vendor[8];
    u8      product[16];
    u8      revision[4];
} __attribute__((packed)) gdth_inq_data;

/* READ_CAPACITY data format */
typedef struct {
    u32     last_block_no;
    u32     block_length;
} __attribute__((packed)) gdth_rdcap_data;

/* READ_CAPACITY (16) data format */
typedef struct {
    u64     last_block_no;
    u32     block_length;
} __attribute__((packed)) gdth_rdcap16_data;

/* REQUEST_SENSE data format */
typedef struct {
    u8      errorcode;
    u8      segno;
    u8      key;
    u32     info;
    u8      add_length;
    u32     cmd_info;
    u8      adsc;
    u8      adsq;
    u8      fruc;
    u8      key_spec[3];
} __attribute__((packed)) gdth_sense_data;

/* MODE_SENSE data format */
typedef struct {
    struct {
        u8  data_length;
        u8  med_type;
        u8  dev_par;
        u8  bd_length;
    } __attribute__((packed)) hd;
    struct {
        u8  dens_code;
        u8  block_count[3];
        u8  reserved;
        u8  block_length[3];
    } __attribute__((packed)) bd;
} __attribute__((packed)) gdth_modep_data;

/* stack frame */
typedef struct {
    unsigned long       b[10];                          /* 32/64 bit compiler ! */
} __attribute__((packed)) gdth_stackframe;


/* function prototyping */

int gdth_show_info(struct seq_file *, struct Scsi_Host *);
int gdth_set_info(struct Scsi_Host *, char *, int);

#endif
