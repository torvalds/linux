/* 
 * File...........: linux/drivers/s390/block/dasd_eckd.h
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *                  Horst Hummel <Horst.Hummel@de.ibm.com> 
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000
 *
 */

#ifndef DASD_ECKD_H
#define DASD_ECKD_H

/*****************************************************************************
 * SECTION: CCW Definitions
 ****************************************************************************/
#define DASD_ECKD_CCW_WRITE		 0x05
#define DASD_ECKD_CCW_READ		 0x06
#define DASD_ECKD_CCW_WRITE_HOME_ADDRESS 0x09
#define DASD_ECKD_CCW_READ_HOME_ADDRESS	 0x0a
#define DASD_ECKD_CCW_WRITE_KD		 0x0d
#define DASD_ECKD_CCW_READ_KD		 0x0e
#define DASD_ECKD_CCW_ERASE		 0x11
#define DASD_ECKD_CCW_READ_COUNT	 0x12
#define DASD_ECKD_CCW_SLCK		 0x14
#define DASD_ECKD_CCW_WRITE_RECORD_ZERO	 0x15
#define DASD_ECKD_CCW_READ_RECORD_ZERO	 0x16
#define DASD_ECKD_CCW_WRITE_CKD		 0x1d
#define DASD_ECKD_CCW_READ_CKD		 0x1e
#define DASD_ECKD_CCW_PSF		 0x27
#define DASD_ECKD_CCW_RSSD		 0x3e
#define DASD_ECKD_CCW_LOCATE_RECORD	 0x47
#define DASD_ECKD_CCW_SNSS               0x54
#define DASD_ECKD_CCW_DEFINE_EXTENT	 0x63
#define DASD_ECKD_CCW_WRITE_MT		 0x85
#define DASD_ECKD_CCW_READ_MT		 0x86
#define DASD_ECKD_CCW_WRITE_KD_MT	 0x8d
#define DASD_ECKD_CCW_READ_KD_MT	 0x8e
#define DASD_ECKD_CCW_RELEASE		 0x94
#define DASD_ECKD_CCW_READ_CKD_MT	 0x9e
#define DASD_ECKD_CCW_WRITE_CKD_MT	 0x9d
#define DASD_ECKD_CCW_RESERVE		 0xB4

/*
 *Perform Subsystem Function / Sub-Orders
 */
#define PSF_ORDER_PRSSD			 0x18

/*****************************************************************************
 * SECTION: Type Definitions
 ****************************************************************************/

struct eckd_count {
	__u16 cyl;
	__u16 head;
	__u8 record;
	__u8 kl;
	__u16 dl;
} __attribute__ ((packed));

struct ch_t {
	__u16 cyl;
	__u16 head;
} __attribute__ ((packed));

struct chs_t {
	__u16 cyl;
	__u16 head;
	__u32 sector;
} __attribute__ ((packed));

struct chr_t {
	__u16 cyl;
	__u16 head;
	__u8 record;
} __attribute__ ((packed));

struct geom_t {
	__u16 cyl;
	__u16 head;
	__u32 sector;
} __attribute__ ((packed));

struct eckd_home {
	__u8 skip_control[14];
	__u16 cell_number;
	__u8 physical_addr[3];
	__u8 flag;
	struct ch_t track_addr;
	__u8 reserved;
	__u8 key_length;
	__u8 reserved2[2];
} __attribute__ ((packed));

struct DE_eckd_data {
	struct {
		unsigned char perm:2;	/* Permissions on this extent */
		unsigned char reserved:1;
		unsigned char seek:2;	/* Seek control */
		unsigned char auth:2;	/* Access authorization */
		unsigned char pci:1;	/* PCI Fetch mode */
	} __attribute__ ((packed)) mask;
	struct {
		unsigned char mode:2;	/* Architecture mode */
		unsigned char ckd:1;	/* CKD Conversion */
		unsigned char operation:3;	/* Operation mode */
		unsigned char cfw:1;	/* Cache fast write */
		unsigned char dfw:1;	/* DASD fast write */
	} __attribute__ ((packed)) attributes;
	__u16 blk_size;		/* Blocksize */
	__u16 fast_write_id;
	__u8 ga_additional;	/* Global Attributes Additional */
	__u8 ga_extended;	/* Global Attributes Extended	*/
	struct ch_t beg_ext;
	struct ch_t end_ext;
	unsigned long long ep_sys_time; /* Ext Parameter - System Time Stamp */
	__u8 ep_format;        /* Extended Parameter format byte       */
	__u8 ep_prio;          /* Extended Parameter priority I/O byte */
	__u8 ep_reserved[6];   /* Extended Parameter Reserved          */
} __attribute__ ((packed));

struct LO_eckd_data {
	struct {
		unsigned char orientation:2;
		unsigned char operation:6;
	} __attribute__ ((packed)) operation;
	struct {
		unsigned char last_bytes_used:1;
		unsigned char reserved:6;
		unsigned char read_count_suffix:1;
	} __attribute__ ((packed)) auxiliary;
	__u8 unused;
	__u8 count;
	struct ch_t seek_addr;
	struct chr_t search_arg;
	__u8 sector;
	__u16 length;
} __attribute__ ((packed));

struct dasd_eckd_characteristics {
	__u16 cu_type;
	struct {
		unsigned char support:2;
		unsigned char async:1;
		unsigned char reserved:1;
		unsigned char cache_info:1;
		unsigned char model:3;
	} __attribute__ ((packed)) cu_model;
	__u16 dev_type;
	__u8 dev_model;
	struct {
		unsigned char mult_burst:1;
		unsigned char RT_in_LR:1;
		unsigned char reserved1:1;
		unsigned char RD_IN_LR:1;
		unsigned char reserved2:4;
		unsigned char reserved3:8;
		unsigned char defect_wr:1;
		unsigned char XRC_supported:1; 
		unsigned char reserved4:1;
		unsigned char striping:1;
		unsigned char reserved5:4;
		unsigned char cfw:1;
		unsigned char reserved6:2;
		unsigned char cache:1;
		unsigned char dual_copy:1;
		unsigned char dfw:1;
		unsigned char reset_alleg:1;
		unsigned char sense_down:1;
	} __attribute__ ((packed)) facilities;
	__u8 dev_class;
	__u8 unit_type;
	__u16 no_cyl;
	__u16 trk_per_cyl;
	__u8 sec_per_trk;
	__u8 byte_per_track[3];
	__u16 home_bytes;
	__u8 formula;
	union {
		struct {
			__u8 f1;
			__u16 f2;
			__u16 f3;
		} __attribute__ ((packed)) f_0x01;
		struct {
			__u8 f1;
			__u8 f2;
			__u8 f3;
			__u8 f4;
			__u8 f5;
		} __attribute__ ((packed)) f_0x02;
	} __attribute__ ((packed)) factors;
	__u16 first_alt_trk;
	__u16 no_alt_trk;
	__u16 first_dia_trk;
	__u16 no_dia_trk;
	__u16 first_sup_trk;
	__u16 no_sup_trk;
	__u8 MDR_ID;
	__u8 OBR_ID;
	__u8 director;
	__u8 rd_trk_set;
	__u16 max_rec_zero;
	__u8 reserved1;
	__u8 RWANY_in_LR;
	__u8 factor6;
	__u8 factor7;
	__u8 factor8;
	__u8 reserved2[3];
	__u8 reserved3[10];
} __attribute__ ((packed));

struct dasd_eckd_confdata {
	struct {
		struct {
			unsigned char identifier:2;
			unsigned char token_id:1;
			unsigned char sno_valid:1;
			unsigned char subst_sno:1;
			unsigned char recNED:1;
			unsigned char emuNED:1;
			unsigned char reserved:1;
		} __attribute__ ((packed)) flags;
		__u8 descriptor;
		__u8 dev_class;
		__u8 reserved;
		unsigned char dev_type[6];
		unsigned char dev_model[3];
		unsigned char HDA_manufacturer[3];
		unsigned char HDA_location[2];
		unsigned char HDA_seqno[12];
		__u16 ID;
	} __attribute__ ((packed)) ned1;
	struct {
		struct {
			unsigned char identifier:2;
			unsigned char token_id:1;
			unsigned char sno_valid:1;
			unsigned char subst_sno:1;
			unsigned char recNED:1;
			unsigned char emuNED:1;
			unsigned char reserved:1;
		} __attribute__ ((packed)) flags;
		__u8 descriptor;
		__u8 reserved[2];
		unsigned char dev_type[6];
		unsigned char dev_model[3];
		unsigned char DASD_manufacturer[3];
		unsigned char DASD_location[2];
		unsigned char DASD_seqno[12];
		__u16 ID;
	} __attribute__ ((packed)) ned2;
	struct {
		struct {
			unsigned char identifier:2;
			unsigned char token_id:1;
			unsigned char sno_valid:1;
			unsigned char subst_sno:1;
			unsigned char recNED:1;
			unsigned char emuNED:1;
			unsigned char reserved:1;
		} __attribute__ ((packed)) flags;
		__u8 descriptor;
		__u8 reserved[2];
		unsigned char cont_type[6];
		unsigned char cont_model[3];
		unsigned char cont_manufacturer[3];
		unsigned char cont_location[2];
		unsigned char cont_seqno[12];
		__u16 ID;
	} __attribute__ ((packed)) ned3;
	struct {
		struct {
			unsigned char identifier:2;
			unsigned char token_id:1;
			unsigned char sno_valid:1;
			unsigned char subst_sno:1;
			unsigned char recNED:1;
			unsigned char emuNED:1;
			unsigned char reserved:1;
		} __attribute__ ((packed)) flags;
		__u8 descriptor;
		__u8 reserved[2];
		unsigned char cont_type[6];
		unsigned char empty[3];
		unsigned char cont_manufacturer[3];
		unsigned char cont_location[2];
		unsigned char cont_seqno[12];
		__u16 ID;
	} __attribute__ ((packed)) ned4;
	unsigned char ned5[32];
	unsigned char ned6[32];
	unsigned char ned7[32];
	struct {
		struct {
			unsigned char identifier:2;
			unsigned char reserved:6;
		} __attribute__ ((packed)) flags;
		__u8 selector;
		__u16 interfaceID;
		__u32 reserved;
		__u16 subsystemID;
		struct {
			unsigned char sp0:1;
			unsigned char sp1:1;
			unsigned char reserved:5;
			unsigned char scluster:1;
		} __attribute__ ((packed)) spathID;
		__u8 unit_address;
		__u8 dev_ID;
		__u8 dev_address;
		__u8 adapterID;
		__u16 link_address;
		struct {
			unsigned char parallel:1;
			unsigned char escon:1;
			unsigned char reserved:1;
			unsigned char ficon:1;
			unsigned char reserved2:4;
		} __attribute__ ((packed)) protocol_type;
		struct {
			unsigned char PID_in_236:1;
			unsigned char reserved:7;
		} __attribute__ ((packed)) format_flags;
		__u8 log_dev_address;
		unsigned char reserved2[12];
	} __attribute__ ((packed)) neq;
} __attribute__ ((packed));

struct dasd_eckd_path {
	__u8 opm;
	__u8 ppm;
	__u8 npm;
};

/*
 * Perform Subsystem Function - Prepare for Read Subsystem Data	 
 */
struct dasd_psf_prssd_data {
	unsigned char order;
	unsigned char flags;
	unsigned char reserved[4];
	unsigned char suborder;
	unsigned char varies[9];
} __attribute__ ((packed));

#endif				/* DASD_ECKD_H */
