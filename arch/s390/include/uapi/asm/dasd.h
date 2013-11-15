/* 
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * Copyright IBM Corp. 1999, 2000
 * EMC Symmetrix ioctl Copyright EMC Corporation, 2008
 * Author.........: Nigel Hislop <hislop_nigel@emc.com>
 *
 * This file is the interface of the DASD device driver, which is exported to user space
 * any future changes wrt the API will result in a change of the APIVERSION reported
 * to userspace by the DASDAPIVER-ioctl
 *
 */

#ifndef DASD_H
#define DASD_H
#include <linux/types.h>
#include <linux/ioctl.h>

#define DASD_IOCTL_LETTER 'D'

#define DASD_API_VERSION 6

/* 
 * struct dasd_information2_t
 * represents any data about the device, which is visible to userspace.
 *  including foramt and featueres.
 */
typedef struct dasd_information2_t {
        unsigned int devno;         /* S/390 devno */
        unsigned int real_devno;    /* for aliases */
        unsigned int schid;         /* S/390 subchannel identifier */
        unsigned int cu_type  : 16; /* from SenseID */
        unsigned int cu_model :  8; /* from SenseID */
        unsigned int dev_type : 16; /* from SenseID */
        unsigned int dev_model : 8; /* from SenseID */
        unsigned int open_count; 
        unsigned int req_queue_len; 
        unsigned int chanq_len;     /* length of chanq */
        char type[4];               /* from discipline.name, 'none' for unknown */
        unsigned int status;        /* current device level */
        unsigned int label_block;   /* where to find the VOLSER */
        unsigned int FBA_layout;    /* fixed block size (like AIXVOL) */
        unsigned int characteristics_size;
        unsigned int confdata_size;
        char characteristics[64];   /* from read_device_characteristics */
        char configuration_data[256]; /* from read_configuration_data */
        unsigned int format;          /* format info like formatted/cdl/ldl/... */
        unsigned int features;        /* dasd features like 'ro',...            */
        unsigned int reserved0;       /* reserved for further use ,...          */
        unsigned int reserved1;       /* reserved for further use ,...          */
        unsigned int reserved2;       /* reserved for further use ,...          */
        unsigned int reserved3;       /* reserved for further use ,...          */
        unsigned int reserved4;       /* reserved for further use ,...          */
        unsigned int reserved5;       /* reserved for further use ,...          */
        unsigned int reserved6;       /* reserved for further use ,...          */
        unsigned int reserved7;       /* reserved for further use ,...          */
} dasd_information2_t;

/*
 * values to be used for dasd_information_t.format
 * 0x00: NOT formatted
 * 0x01: Linux disc layout
 * 0x02: Common disc layout
 */
#define DASD_FORMAT_NONE 0
#define DASD_FORMAT_LDL  1
#define DASD_FORMAT_CDL  2
/*
 * values to be used for dasd_information_t.features
 * 0x00: default features
 * 0x01: readonly (ro)
 * 0x02: use diag discipline (diag)
 * 0x04: set the device initially online (internal use only)
 * 0x08: enable ERP related logging
 * 0x20: give access to raw eckd data
 */
#define DASD_FEATURE_DEFAULT	     0x00
#define DASD_FEATURE_READONLY	     0x01
#define DASD_FEATURE_USEDIAG	     0x02
#define DASD_FEATURE_INITIAL_ONLINE  0x04
#define DASD_FEATURE_ERPLOG	     0x08
#define DASD_FEATURE_FAILFAST	     0x10
#define DASD_FEATURE_FAILONSLCK      0x20
#define DASD_FEATURE_USERAW	     0x40

#define DASD_PARTN_BITS 2

/* 
 * struct dasd_information_t
 * represents any data about the data, which is visible to userspace
 */
typedef struct dasd_information_t {
        unsigned int devno;         /* S/390 devno */
        unsigned int real_devno;    /* for aliases */
        unsigned int schid;         /* S/390 subchannel identifier */
        unsigned int cu_type  : 16; /* from SenseID */
        unsigned int cu_model :  8; /* from SenseID */
        unsigned int dev_type : 16; /* from SenseID */
        unsigned int dev_model : 8; /* from SenseID */
        unsigned int open_count; 
        unsigned int req_queue_len; 
        unsigned int chanq_len;     /* length of chanq */
        char type[4];               /* from discipline.name, 'none' for unknown */
        unsigned int status;        /* current device level */
        unsigned int label_block;   /* where to find the VOLSER */
        unsigned int FBA_layout;    /* fixed block size (like AIXVOL) */
        unsigned int characteristics_size;
        unsigned int confdata_size;
        char characteristics[64];   /* from read_device_characteristics */
        char configuration_data[256]; /* from read_configuration_data */
} dasd_information_t;

/*
 * Read Subsystem Data - Performance Statistics
 */ 
typedef struct dasd_rssd_perf_stats_t {
	unsigned char  invalid:1;
	unsigned char  format:3;
	unsigned char  data_format:4;
	unsigned char  unit_address;
	unsigned short device_status;
	unsigned int   nr_read_normal;
	unsigned int   nr_read_normal_hits;
	unsigned int   nr_write_normal;
	unsigned int   nr_write_fast_normal_hits;
	unsigned int   nr_read_seq;
	unsigned int   nr_read_seq_hits;
	unsigned int   nr_write_seq;
	unsigned int   nr_write_fast_seq_hits;
	unsigned int   nr_read_cache;
	unsigned int   nr_read_cache_hits;
	unsigned int   nr_write_cache;
	unsigned int   nr_write_fast_cache_hits;
	unsigned int   nr_inhibit_cache;
	unsigned int   nr_bybass_cache;
	unsigned int   nr_seq_dasd_to_cache;
	unsigned int   nr_dasd_to_cache;
	unsigned int   nr_cache_to_dasd;
	unsigned int   nr_delayed_fast_write;
	unsigned int   nr_normal_fast_write;
	unsigned int   nr_seq_fast_write;
	unsigned int   nr_cache_miss;
	unsigned char  status2;
	unsigned int   nr_quick_write_promotes;
	unsigned char  reserved;
	unsigned short ssid;
	unsigned char  reseved2[96];
} __attribute__((packed)) dasd_rssd_perf_stats_t;

/* 
 * struct profile_info_t
 * holds the profinling information 
 */
typedef struct dasd_profile_info_t {
        unsigned int dasd_io_reqs;	 /* number of requests processed at all */
        unsigned int dasd_io_sects;	 /* number of sectors processed at all */
        unsigned int dasd_io_secs[32];	 /* histogram of request's sizes */
        unsigned int dasd_io_times[32];	 /* histogram of requests's times */
        unsigned int dasd_io_timps[32];	 /* histogram of requests's times per sector */
        unsigned int dasd_io_time1[32];	 /* histogram of time from build to start */
        unsigned int dasd_io_time2[32];	 /* histogram of time from start to irq */
        unsigned int dasd_io_time2ps[32]; /* histogram of time from start to irq */
        unsigned int dasd_io_time3[32];	 /* histogram of time from irq to end */
        unsigned int dasd_io_nr_req[32]; /* histogram of # of requests in chanq */
} dasd_profile_info_t;

/*
 * struct format_data_t
 * represents all data necessary to format a dasd
 */
typedef struct format_data_t {
	unsigned int start_unit; /* from track */
	unsigned int stop_unit;  /* to track */
	unsigned int blksize;	 /* sectorsize */
	unsigned int intensity;
} format_data_t;

/*
 * values to be used for format_data_t.intensity
 * 0/8: normal format
 * 1/9: also write record zero
 * 3/11: also write home address
 * 4/12: invalidate track
 */
#define DASD_FMT_INT_FMT_R0 1 /* write record zero */
#define DASD_FMT_INT_FMT_HA 2 /* write home address, also set FMT_R0 ! */
#define DASD_FMT_INT_INVAL  4 /* invalidate tracks */
#define DASD_FMT_INT_COMPAT 8 /* use OS/390 compatible disk layout */


/* 
 * struct attrib_data_t
 * represents the operation (cache) bits for the device.
 * Used in DE to influence caching of the DASD.
 */
typedef struct attrib_data_t {
	unsigned char operation:3;     /* cache operation mode */
	unsigned char reserved:5;      /* cache operation mode */
	__u16         nr_cyl;          /* no of cyliners for read ahaed */
	__u8          reserved2[29];   /* for future use */
} __attribute__ ((packed)) attrib_data_t;

/* definition of operation (cache) bits within attributes of DE */
#define DASD_NORMAL_CACHE  0x0
#define DASD_BYPASS_CACHE  0x1
#define DASD_INHIBIT_LOAD  0x2
#define DASD_SEQ_ACCESS    0x3
#define DASD_SEQ_PRESTAGE  0x4
#define DASD_REC_ACCESS    0x5

/*
 * Perform EMC Symmetrix I/O
 */
typedef struct dasd_symmio_parms {
	unsigned char reserved[8];	/* compat with older releases */
	unsigned long long psf_data;	/* char * cast to u64 */
	unsigned long long rssd_result; /* char * cast to u64 */
	int psf_data_len;
	int rssd_result_len;
} __attribute__ ((packed)) dasd_symmio_parms_t;

/*
 * Data returned by Sense Path Group ID (SNID)
 */
struct dasd_snid_data {
	struct {
		__u8 group:2;
		__u8 reserve:2;
		__u8 mode:1;
		__u8 res:3;
	} __attribute__ ((packed)) path_state;
	__u8 pgid[11];
} __attribute__ ((packed));

struct dasd_snid_ioctl_data {
	struct dasd_snid_data data;
	__u8 path_mask;
} __attribute__ ((packed));


/********************************************************************************
 * SECTION: Definition of IOCTLs
 *
 * Here ist how the ioctl-nr should be used:
 *    0 -   31   DASD driver itself
 *   32 -  239   still open
 *  240 -  255   reserved for EMC 
 *******************************************************************************/

/* Disable the volume (for Linux) */
#define BIODASDDISABLE _IO(DASD_IOCTL_LETTER,0) 
/* Enable the volume (for Linux) */
#define BIODASDENABLE  _IO(DASD_IOCTL_LETTER,1)  
/* Issue a reserve/release command, rsp. */
#define BIODASDRSRV    _IO(DASD_IOCTL_LETTER,2) /* reserve */
#define BIODASDRLSE    _IO(DASD_IOCTL_LETTER,3) /* release */
#define BIODASDSLCK    _IO(DASD_IOCTL_LETTER,4) /* steal lock */
/* reset profiling information of a device */
#define BIODASDPRRST   _IO(DASD_IOCTL_LETTER,5)
/* Quiesce IO on device */
#define BIODASDQUIESCE _IO(DASD_IOCTL_LETTER,6) 
/* Resume IO on device */
#define BIODASDRESUME  _IO(DASD_IOCTL_LETTER,7) 
/* Abort all I/O on a device */
#define BIODASDABORTIO _IO(DASD_IOCTL_LETTER, 240)
/* Allow I/O on a device */
#define BIODASDALLOWIO _IO(DASD_IOCTL_LETTER, 241)


/* retrieve API version number */
#define DASDAPIVER     _IOR(DASD_IOCTL_LETTER,0,int)
/* Get information on a dasd device */
#define BIODASDINFO    _IOR(DASD_IOCTL_LETTER,1,dasd_information_t)
/* retrieve profiling information of a device */
#define BIODASDPRRD    _IOR(DASD_IOCTL_LETTER,2,dasd_profile_info_t)
/* Get information on a dasd device (enhanced) */
#define BIODASDINFO2   _IOR(DASD_IOCTL_LETTER,3,dasd_information2_t)
/* Performance Statistics Read */
#define BIODASDPSRD    _IOR(DASD_IOCTL_LETTER,4,dasd_rssd_perf_stats_t)
/* Get Attributes (cache operations) */
#define BIODASDGATTR   _IOR(DASD_IOCTL_LETTER,5,attrib_data_t) 


/* #define BIODASDFORMAT  _IOW(IOCTL_LETTER,0,format_data_t) , deprecated */
#define BIODASDFMT     _IOW(DASD_IOCTL_LETTER,1,format_data_t) 
/* Set Attributes (cache operations) */
#define BIODASDSATTR   _IOW(DASD_IOCTL_LETTER,2,attrib_data_t) 

/* Get Sense Path Group ID (SNID) data */
#define BIODASDSNID    _IOWR(DASD_IOCTL_LETTER, 1, struct dasd_snid_ioctl_data)

#define BIODASDSYMMIO  _IOWR(DASD_IOCTL_LETTER, 240, dasd_symmio_parms_t)

#endif				/* DASD_H */

