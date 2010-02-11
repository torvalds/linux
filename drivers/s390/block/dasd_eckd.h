/*
 * File...........: linux/drivers/s390/block/dasd_eckd.h
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
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
#define DASD_ECKD_CCW_SNSS		 0x54
#define DASD_ECKD_CCW_DEFINE_EXTENT	 0x63
#define DASD_ECKD_CCW_WRITE_MT		 0x85
#define DASD_ECKD_CCW_READ_MT		 0x86
#define DASD_ECKD_CCW_WRITE_KD_MT	 0x8d
#define DASD_ECKD_CCW_READ_KD_MT	 0x8e
#define DASD_ECKD_CCW_RELEASE		 0x94
#define DASD_ECKD_CCW_READ_CKD_MT	 0x9e
#define DASD_ECKD_CCW_WRITE_CKD_MT	 0x9d
#define DASD_ECKD_CCW_WRITE_TRACK_DATA	 0xA5
#define DASD_ECKD_CCW_READ_TRACK_DATA	 0xA6
#define DASD_ECKD_CCW_RESERVE		 0xB4
#define DASD_ECKD_CCW_PFX		 0xE7
#define DASD_ECKD_CCW_PFX_READ		 0xEA
#define DASD_ECKD_CCW_RSCK		 0xF9

/*
 * Perform Subsystem Function / Sub-Orders
 */
#define PSF_ORDER_PRSSD 0x18
#define PSF_ORDER_SSC	0x1D

/*
 * Size that is reportet for large volumes in the old 16-bit no_cyl field
 */
#define LV_COMPAT_CYL 0xFFFE

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
	__u8 ep_reserved1;     /* Extended Parameter Reserved	       */
	__u8 ep_rec_per_track; /* Number of records on a track	       */
	__u8 ep_reserved[4];   /* Extended Parameter Reserved	       */
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

struct LRE_eckd_data {
	struct {
		unsigned char orientation:2;
		unsigned char operation:6;
	} __attribute__ ((packed)) operation;
	struct {
		unsigned char length_valid:1;
		unsigned char length_scope:1;
		unsigned char imbedded_ccw_valid:1;
		unsigned char check_bytes:2;
		unsigned char imbedded_count_valid:1;
		unsigned char reserved:1;
		unsigned char read_count_suffix:1;
	} __attribute__ ((packed)) auxiliary;
	__u8 imbedded_ccw;
	__u8 count;
	struct ch_t seek_addr;
	struct chr_t search_arg;
	__u8 sector;
	__u16 length;
	__u8 imbedded_count;
	__u8 extended_operation;
	__u16 extended_parameter_length;
	__u8 extended_parameter[0];
} __attribute__ ((packed));

/* Prefix data for format 0x00 and 0x01 */
struct PFX_eckd_data {
	unsigned char format;
	struct {
		unsigned char define_extent:1;
		unsigned char time_stamp:1;
		unsigned char verify_base:1;
		unsigned char hyper_pav:1;
		unsigned char reserved:4;
	} __attribute__ ((packed)) validity;
	__u8 base_address;
	__u8 aux;
	__u8 base_lss;
	__u8 reserved[7];
	struct DE_eckd_data define_extent;
	struct LRE_eckd_data locate_record;
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
	__u8 reserved3[6];
	__u32 long_no_cyl;
} __attribute__ ((packed));

/* elements of the configuration data */
struct dasd_ned {
	struct {
		__u8 identifier:2;
		__u8 token_id:1;
		__u8 sno_valid:1;
		__u8 subst_sno:1;
		__u8 recNED:1;
		__u8 emuNED:1;
		__u8 reserved:1;
	} __attribute__ ((packed)) flags;
	__u8 descriptor;
	__u8 dev_class;
	__u8 reserved;
	__u8 dev_type[6];
	__u8 dev_model[3];
	__u8 HDA_manufacturer[3];
	__u8 HDA_location[2];
	__u8 HDA_seqno[12];
	__u8 ID;
	__u8 unit_addr;
} __attribute__ ((packed));

struct dasd_sneq {
	struct {
		__u8 identifier:2;
		__u8 reserved:6;
	} __attribute__ ((packed)) flags;
	__u8 res1;
	__u16 format;
	__u8 res2[4];		/* byte  4- 7 */
	__u8 sua_flags;		/* byte  8    */
	__u8 base_unit_addr;	/* byte  9    */
	__u8 res3[22];		/* byte 10-31 */
} __attribute__ ((packed));

struct vd_sneq {
	struct {
		__u8 identifier:2;
		__u8 reserved:6;
	} __attribute__ ((packed)) flags;
	__u8 res1;
	__u16 format;
	__u8 res2[4];	/* byte  4- 7 */
	__u8 uit[16];	/* byte  8-23 */
	__u8 res3[8];	/* byte 24-31 */
} __attribute__ ((packed));

struct dasd_gneq {
	struct {
		__u8 identifier:2;
		__u8 reserved:6;
	} __attribute__ ((packed)) flags;
	__u8 reserved[7];
	__u16 subsystemID;
	__u8 reserved2[22];
} __attribute__ ((packed));

struct dasd_eckd_path {
	__u8 opm;
	__u8 ppm;
	__u8 npm;
};

struct dasd_rssd_features {
	char feature[256];
} __attribute__((packed));


/*
 * Perform Subsystem Function - Prepare for Read Subsystem Data
 */
struct dasd_psf_prssd_data {
	unsigned char order;
	unsigned char flags;
	unsigned char reserved[4];
	unsigned char suborder;
	unsigned char varies[5];
} __attribute__ ((packed));

/*
 * Perform Subsystem Function - Set Subsystem Characteristics
 */
struct dasd_psf_ssc_data {
	unsigned char order;
	unsigned char flags;
	unsigned char cu_type[4];
	unsigned char suborder;
	unsigned char reserved[59];
} __attribute__((packed));


/*
 * some structures and definitions for alias handling
 */
struct dasd_unit_address_configuration {
	struct {
		char ua_type;
		char base_ua;
	} unit[256];
} __attribute__((packed));


#define MAX_DEVICES_PER_LCU 256

/* flags on the LCU  */
#define NEED_UAC_UPDATE  0x01
#define UPDATE_PENDING	0x02

enum pavtype {NO_PAV, BASE_PAV, HYPER_PAV};


struct alias_root {
	struct list_head serverlist;
	spinlock_t lock;
};

struct alias_server {
	struct list_head server;
	struct dasd_uid uid;
	struct list_head lculist;
};

struct summary_unit_check_work_data {
	char reason;
	struct dasd_device *device;
	struct work_struct worker;
};

struct read_uac_work_data {
	struct dasd_device *device;
	struct delayed_work dwork;
};

struct alias_lcu {
	struct list_head lcu;
	struct dasd_uid uid;
	enum pavtype pav;
	char flags;
	spinlock_t lock;
	struct list_head grouplist;
	struct list_head active_devices;
	struct list_head inactive_devices;
	struct dasd_unit_address_configuration *uac;
	struct summary_unit_check_work_data suc_data;
	struct read_uac_work_data ruac_data;
	struct dasd_ccw_req *rsu_cqr;
	struct completion lcu_setup;
};

struct alias_pav_group {
	struct list_head group;
	struct dasd_uid uid;
	struct alias_lcu *lcu;
	struct list_head baselist;
	struct list_head aliaslist;
	struct dasd_device *next;
};


struct dasd_eckd_private {
	struct dasd_eckd_characteristics rdc_data;
	u8 *conf_data;
	int conf_len;
	/* pointers to specific parts in the conf_data */
	struct dasd_ned *ned;
	struct dasd_sneq *sneq;
	struct vd_sneq *vdsneq;
	struct dasd_gneq *gneq;

	struct dasd_eckd_path path_data;
	struct eckd_count count_area[5];
	int init_cqr_status;
	int uses_cdl;
	struct attrib_data_t attrib;	/* e.g. cache operations */
	struct dasd_rssd_features features;
	u32 real_cyl;

	/* alias managemnet */
	struct dasd_uid uid;
	struct alias_pav_group *pavgroup;
	struct alias_lcu *lcu;
	int count;
};



int dasd_alias_make_device_known_to_lcu(struct dasd_device *);
void dasd_alias_disconnect_device_from_lcu(struct dasd_device *);
int dasd_alias_add_device(struct dasd_device *);
int dasd_alias_remove_device(struct dasd_device *);
struct dasd_device *dasd_alias_get_start_dev(struct dasd_device *);
void dasd_alias_handle_summary_unit_check(struct dasd_device *, struct irb *);
void dasd_eckd_reset_ccw_to_base_io(struct dasd_ccw_req *);
void dasd_alias_lcu_setup_complete(struct dasd_device *);
void dasd_alias_wait_for_lcu_setup(struct dasd_device *);
#endif				/* DASD_ECKD_H */
