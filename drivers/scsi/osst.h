/*
 *	$Header: /cvsroot/osst/Driver/osst.h,v 1.16 2005/01/01 21:13:35 wriede Exp $
 */

#include <asm/byteorder.h>
#include <linux/completion.h>
#include <linux/mutex.h>

/*	FIXME - rename and use the following two types or delete them!
 *              and the types really should go to st.h anyway...
 *	INQUIRY packet command - Data Format (From Table 6-8 of QIC-157C)
 */
typedef struct {
	unsigned	device_type	:5;	/* Peripheral Device Type */
	unsigned	reserved0_765	:3;	/* Peripheral Qualifier - Reserved */
	unsigned	reserved1_6t0	:7;	/* Reserved */
	unsigned	rmb		:1;	/* Removable Medium Bit */
	unsigned	ansi_version	:3;	/* ANSI Version */
	unsigned	ecma_version	:3;	/* ECMA Version */
	unsigned	iso_version	:2;	/* ISO Version */
	unsigned	response_format :4;	/* Response Data Format */
	unsigned	reserved3_45	:2;	/* Reserved */
	unsigned	reserved3_6	:1;	/* TrmIOP - Reserved */
	unsigned	reserved3_7	:1;	/* AENC - Reserved */
	u8		additional_length;	/* Additional Length (total_length-4) */
	u8		rsv5, rsv6, rsv7;	/* Reserved */
	u8		vendor_id[8];		/* Vendor Identification */
	u8		product_id[16];		/* Product Identification */
	u8		revision_level[4];	/* Revision Level */
	u8		vendor_specific[20];	/* Vendor Specific - Optional */
	u8		reserved56t95[40];	/* Reserved - Optional */
						/* Additional information may be returned */
} idetape_inquiry_result_t;

/*
 *	READ POSITION packet command - Data Format (From Table 6-57)
 */
typedef struct {
	unsigned	reserved0_10	:2;	/* Reserved */
	unsigned	bpu		:1;	/* Block Position Unknown */	
	unsigned	reserved0_543	:3;	/* Reserved */
	unsigned	eop		:1;	/* End Of Partition */
	unsigned	bop		:1;	/* Beginning Of Partition */
	u8		partition;		/* Partition Number */
	u8		reserved2, reserved3;	/* Reserved */
	u32		first_block;		/* First Block Location */
	u32		last_block;		/* Last Block Location (Optional) */
	u8		reserved12;		/* Reserved */
	u8		blocks_in_buffer[3];	/* Blocks In Buffer - (Optional) */
	u32		bytes_in_buffer;	/* Bytes In Buffer (Optional) */
} idetape_read_position_result_t;

/*
 *      Follows structures which are related to the SELECT SENSE / MODE SENSE
 *      packet commands. 
 */
#define COMPRESSION_PAGE           0x0f
#define COMPRESSION_PAGE_LENGTH    16

#define CAPABILITIES_PAGE          0x2a
#define CAPABILITIES_PAGE_LENGTH   20

#define TAPE_PARAMTR_PAGE          0x2b
#define TAPE_PARAMTR_PAGE_LENGTH   16

#define NUMBER_RETRIES_PAGE        0x2f
#define NUMBER_RETRIES_PAGE_LENGTH 4

#define BLOCK_SIZE_PAGE            0x30
#define BLOCK_SIZE_PAGE_LENGTH     4

#define BUFFER_FILLING_PAGE        0x33
#define BUFFER_FILLING_PAGE_LENGTH 4

#define VENDOR_IDENT_PAGE          0x36
#define VENDOR_IDENT_PAGE_LENGTH   8

#define LOCATE_STATUS_PAGE         0x37
#define LOCATE_STATUS_PAGE_LENGTH  0

#define MODE_HEADER_LENGTH         4


/*
 *	REQUEST SENSE packet command result - Data Format.
 */
typedef struct {
	unsigned	error_code	:7;	/* Current of deferred errors */
	unsigned	valid		:1;	/* The information field conforms to QIC-157C */
	u8		reserved1	:8;	/* Segment Number - Reserved */
	unsigned	sense_key	:4;	/* Sense Key */
	unsigned	reserved2_4	:1;	/* Reserved */
	unsigned	ili		:1;	/* Incorrect Length Indicator */
	unsigned	eom		:1;	/* End Of Medium */
	unsigned	filemark 	:1;	/* Filemark */
	u32		information __attribute__ ((packed));
	u8		asl;			/* Additional sense length (n-7) */
	u32		command_specific;	/* Additional command specific information */
	u8		asc;			/* Additional Sense Code */
	u8		ascq;			/* Additional Sense Code Qualifier */
	u8		replaceable_unit_code;	/* Field Replaceable Unit Code */
	unsigned	sk_specific1 	:7;	/* Sense Key Specific */
	unsigned	sksv		:1;	/* Sense Key Specific information is valid */
	u8		sk_specific2;		/* Sense Key Specific */
	u8		sk_specific3;		/* Sense Key Specific */
	u8		pad[2];			/* Padding to 20 bytes */
} idetape_request_sense_result_t;

/*
 *      Mode Parameter Header for the MODE SENSE packet command
 */
typedef struct {
        u8              mode_data_length;       /* Length of the following data transfer */
        u8              medium_type;            /* Medium Type */
        u8              dsp;                    /* Device Specific Parameter */
        u8              bdl;                    /* Block Descriptor Length */
} osst_mode_parameter_header_t;

/*
 *      Mode Parameter Block Descriptor the MODE SENSE packet command
 *
 *      Support for block descriptors is optional.
 */
typedef struct {
        u8              density_code;           /* Medium density code */
        u8              blocks[3];              /* Number of blocks */
        u8              reserved4;              /* Reserved */
        u8              length[3];              /* Block Length */
} osst_parameter_block_descriptor_t;

/*
 *      The Data Compression Page, as returned by the MODE SENSE packet command.
 */
typedef struct {
#if   defined(__BIG_ENDIAN_BITFIELD)
        unsigned        ps              :1;
        unsigned        reserved0       :1;     /* Reserved */
	unsigned        page_code       :6;     /* Page Code - Should be 0xf */
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned        page_code       :6;     /* Page Code - Should be 0xf */
        unsigned        reserved0       :1;     /* Reserved */
        unsigned        ps              :1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
        u8              page_length;            /* Page Length - Should be 14 */
#if   defined(__BIG_ENDIAN_BITFIELD)
        unsigned        dce             :1;     /* Data Compression Enable */
        unsigned        dcc             :1;     /* Data Compression Capable */
	unsigned        reserved2       :6;     /* Reserved */
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned        reserved2       :6;     /* Reserved */
        unsigned        dcc             :1;     /* Data Compression Capable */
        unsigned        dce             :1;     /* Data Compression Enable */
#else
#error "Please fix <asm/byteorder.h>"
#endif
#if   defined(__BIG_ENDIAN_BITFIELD)
        unsigned        dde             :1;     /* Data Decompression Enable */
        unsigned        red             :2;     /* Report Exception on Decompression */
	unsigned        reserved3       :5;     /* Reserved */
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned        reserved3       :5;     /* Reserved */
        unsigned        red             :2;     /* Report Exception on Decompression */
        unsigned        dde             :1;     /* Data Decompression Enable */
#else
#error "Please fix <asm/byteorder.h>"
#endif
        u32             ca;                     /* Compression Algorithm */
        u32             da;                     /* Decompression Algorithm */
        u8              reserved[4];            /* Reserved */
} osst_data_compression_page_t;

/*
 *      The Medium Partition Page, as returned by the MODE SENSE packet command.
 */
typedef struct {
#if   defined(__BIG_ENDIAN_BITFIELD)
        unsigned        ps              :1;
        unsigned        reserved1_6     :1;     /* Reserved */
	unsigned        page_code       :6;     /* Page Code - Should be 0x11 */
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned        page_code       :6;     /* Page Code - Should be 0x11 */
        unsigned        reserved1_6     :1;     /* Reserved */
        unsigned        ps              :1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
        u8              page_length;            /* Page Length - Should be 6 */
        u8              map;                    /* Maximum Additional Partitions - Should be 0 */
        u8              apd;                    /* Additional Partitions Defined - Should be 0 */
#if   defined(__BIG_ENDIAN_BITFIELD)
        unsigned        fdp             :1;     /* Fixed Data Partitions */
        unsigned        sdp             :1;     /* Should be 0 */
        unsigned        idp             :1;     /* Should be 0 */
        unsigned        psum            :2;     /* Should be 0 */
	unsigned        reserved4_012   :3;     /* Reserved */
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned        reserved4_012   :3;     /* Reserved */
        unsigned        psum            :2;     /* Should be 0 */
        unsigned        idp             :1;     /* Should be 0 */
        unsigned        sdp             :1;     /* Should be 0 */
        unsigned        fdp             :1;     /* Fixed Data Partitions */
#else
#error "Please fix <asm/byteorder.h>"
#endif
        u8              mfr;                    /* Medium Format Recognition */
        u8              reserved[2];            /* Reserved */
} osst_medium_partition_page_t;

/*
 *      Capabilities and Mechanical Status Page
 */
typedef struct {
#if   defined(__BIG_ENDIAN_BITFIELD)
        unsigned        reserved1_67    :2;
	unsigned        page_code       :6;     /* Page code - Should be 0x2a */
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned        page_code       :6;     /* Page code - Should be 0x2a */
        unsigned        reserved1_67    :2;
#else
#error "Please fix <asm/byteorder.h>"
#endif
        u8              page_length;            /* Page Length - Should be 0x12 */
        u8              reserved2, reserved3;
#if   defined(__BIG_ENDIAN_BITFIELD)
        unsigned        reserved4_67    :2;
        unsigned        sprev           :1;     /* Supports SPACE in the reverse direction */
        unsigned        reserved4_1234  :4;
	unsigned        ro              :1;     /* Read Only Mode */
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned        ro              :1;     /* Read Only Mode */
        unsigned        reserved4_1234  :4;
        unsigned        sprev           :1;     /* Supports SPACE in the reverse direction */
        unsigned        reserved4_67    :2;
#else
#error "Please fix <asm/byteorder.h>"
#endif
#if   defined(__BIG_ENDIAN_BITFIELD)
        unsigned        reserved5_67    :2;
        unsigned        qfa             :1;     /* Supports the QFA two partition formats */
        unsigned        reserved5_4     :1;
        unsigned        efmt            :1;     /* Supports ERASE command initiated formatting */
	unsigned        reserved5_012   :3;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned        reserved5_012   :3;
        unsigned        efmt            :1;     /* Supports ERASE command initiated formatting */
        unsigned        reserved5_4     :1;
        unsigned        qfa             :1;     /* Supports the QFA two partition formats */
        unsigned        reserved5_67    :2;
#else
#error "Please fix <asm/byteorder.h>"
#endif
#if   defined(__BIG_ENDIAN_BITFIELD)
        unsigned        cmprs           :1;     /* Supports data compression */
        unsigned        ecc             :1;     /* Supports error correction */
	unsigned        reserved6_45    :2;     /* Reserved */  
        unsigned        eject           :1;     /* The device can eject the volume */
        unsigned        prevent         :1;     /* The device defaults in the prevent state after power up */
        unsigned        locked          :1;     /* The volume is locked */
	unsigned        lock            :1;     /* Supports locking the volume */
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned        lock            :1;     /* Supports locking the volume */
        unsigned        locked          :1;     /* The volume is locked */
        unsigned        prevent         :1;     /* The device defaults in the prevent state after power up */
        unsigned        eject           :1;     /* The device can eject the volume */
	unsigned        reserved6_45    :2;     /* Reserved */  
        unsigned        ecc             :1;     /* Supports error correction */
        unsigned        cmprs           :1;     /* Supports data compression */
#else
#error "Please fix <asm/byteorder.h>"
#endif
#if   defined(__BIG_ENDIAN_BITFIELD)
        unsigned        blk32768        :1;     /* slowb - the device restricts the byte count for PIO */
                                                /* transfers for slow buffer memory ??? */
                                                /* Also 32768 block size in some cases */
        unsigned        reserved7_3_6   :4;
        unsigned        blk1024         :1;     /* Supports 1024 bytes block size */
        unsigned        blk512          :1;     /* Supports 512 bytes block size */
	unsigned        reserved7_0     :1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned        reserved7_0     :1;
        unsigned        blk512          :1;     /* Supports 512 bytes block size */
        unsigned        blk1024         :1;     /* Supports 1024 bytes block size */
        unsigned        reserved7_3_6   :4;
        unsigned        blk32768        :1;     /* slowb - the device restricts the byte count for PIO */
                                                /* transfers for slow buffer memory ??? */
                                                /* Also 32768 block size in some cases */
#else
#error "Please fix <asm/byteorder.h>"
#endif
        __be16          max_speed;              /* Maximum speed supported in KBps */
        u8              reserved10, reserved11;
        __be16          ctl;                    /* Continuous Transfer Limit in blocks */
        __be16          speed;                  /* Current Speed, in KBps */
        __be16          buffer_size;            /* Buffer Size, in 512 bytes */
        u8              reserved18, reserved19;
} osst_capabilities_page_t;

/*
 *      Block Size Page
 */
typedef struct {
#if   defined(__BIG_ENDIAN_BITFIELD)
        unsigned        ps              :1;
        unsigned        reserved1_6     :1;
	unsigned        page_code       :6;     /* Page code - Should be 0x30 */
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned        page_code       :6;     /* Page code - Should be 0x30 */
        unsigned        reserved1_6     :1;
        unsigned        ps              :1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
        u8              page_length;            /* Page Length - Should be 2 */
        u8              reserved2;
#if   defined(__BIG_ENDIAN_BITFIELD)
        unsigned        one             :1;
        unsigned        reserved2_6     :1;
        unsigned        record32_5      :1;
        unsigned        record32        :1;
        unsigned        reserved2_23    :2;
        unsigned        play32_5        :1;
	unsigned        play32          :1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned        play32          :1;
        unsigned        play32_5        :1;
        unsigned        reserved2_23    :2;
        unsigned        record32        :1;
        unsigned        record32_5      :1;
        unsigned        reserved2_6     :1;
        unsigned        one             :1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
} osst_block_size_page_t;

/*
 *	Tape Parameters Page
 */
typedef struct {
#if   defined(__BIG_ENDIAN_BITFIELD)
        unsigned        ps              :1;
        unsigned        reserved1_6     :1;
	unsigned        page_code       :6;     /* Page code - Should be 0x2b */
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned        page_code       :6;     /* Page code - Should be 0x2b */
        unsigned        reserved1_6     :1;
        unsigned        ps              :1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	u8		reserved2;
	u8		density;
	u8		reserved3,reserved4;
	__be16		segtrk;
	__be16		trks;
	u8		reserved5,reserved6,reserved7,reserved8,reserved9,reserved10;
} osst_tape_paramtr_page_t;

/* OnStream definitions */

#define OS_CONFIG_PARTITION     (0xff)
#define OS_DATA_PARTITION       (0)
#define OS_PARTITION_VERSION    (1)

/*
 * partition
 */
typedef struct os_partition_s {
        __u8    partition_num;
        __u8    par_desc_ver;
        __be16  wrt_pass_cntr;
        __be32  first_frame_ppos;
        __be32  last_frame_ppos;
        __be32  eod_frame_ppos;
} os_partition_t;

/*
 * DAT entry
 */
typedef struct os_dat_entry_s {
        __be32  blk_sz;
        __be16  blk_cnt;
        __u8    flags;
        __u8    reserved;
} os_dat_entry_t;

/*
 * DAT
 */
#define OS_DAT_FLAGS_DATA       (0xc)
#define OS_DAT_FLAGS_MARK       (0x1)

typedef struct os_dat_s {
        __u8            dat_sz;
        __u8            reserved1;
        __u8            entry_cnt;
        __u8            reserved3;
        os_dat_entry_t  dat_list[16];
} os_dat_t;

/*
 * Frame types
 */
#define OS_FRAME_TYPE_FILL      (0)
#define OS_FRAME_TYPE_EOD       (1 << 0)
#define OS_FRAME_TYPE_MARKER    (1 << 1)
#define OS_FRAME_TYPE_HEADER    (1 << 3)
#define OS_FRAME_TYPE_DATA      (1 << 7)

/*
 * AUX
 */
typedef struct os_aux_s {
        __be32          format_id;              /* hardware compability AUX is based on */
        char            application_sig[4];     /* driver used to write this media */
        __be32          hdwr;                   /* reserved */
        __be32          update_frame_cntr;      /* for configuration frame */
        __u8            frame_type;
        __u8            frame_type_reserved;
        __u8            reserved_18_19[2];
        os_partition_t  partition;
        __u8            reserved_36_43[8];
        __be32          frame_seq_num;
        __be32          logical_blk_num_high;
        __be32          logical_blk_num;
        os_dat_t        dat;
        __u8            reserved188_191[4];
        __be32          filemark_cnt;
        __be32          phys_fm;
        __be32          last_mark_ppos;
        __u8            reserved204_223[20];

        /*
         * __u8         app_specific[32];
         *
         * Linux specific fields:
         */
         __be32         next_mark_ppos;         /* when known, points to next marker */
	 __be32		last_mark_lbn;		/* storing log_blk_num of last mark is extends ADR spec */
         __u8           linux_specific[24];

        __u8            reserved_256_511[256];
} os_aux_t;

#define OS_FM_TAB_MAX 1024

typedef struct os_fm_tab_s {
	__u8		fm_part_num;
	__u8		reserved_1;
	__u8		fm_tab_ent_sz;
	__u8		reserved_3;
	__be16		fm_tab_ent_cnt;
	__u8		reserved6_15[10];
	__be32		fm_tab_ent[OS_FM_TAB_MAX];
} os_fm_tab_t;

typedef struct os_ext_trk_ey_s {
	__u8		et_part_num;
	__u8		fmt;
	__be16		fm_tab_off;
	__u8		reserved4_7[4];
	__be32		last_hlb_hi;
	__be32		last_hlb;
	__be32		last_pp;
	__u8		reserved20_31[12];
} os_ext_trk_ey_t;

typedef struct os_ext_trk_tb_s {
	__u8		nr_stream_part;
	__u8		reserved_1;
	__u8		et_ent_sz;
	__u8		reserved3_15[13];
	os_ext_trk_ey_t	dat_ext_trk_ey;
	os_ext_trk_ey_t	qfa_ext_trk_ey;
} os_ext_trk_tb_t;

typedef struct os_header_s {
        char            ident_str[8];
        __u8            major_rev;
        __u8            minor_rev;
	__be16		ext_trk_tb_off;
        __u8            reserved12_15[4];
        __u8            pt_par_num;
        __u8            pt_reserved1_3[3];
        os_partition_t  partition[16];
	__be32		cfg_col_width;
	__be32		dat_col_width;
	__be32		qfa_col_width;
	__u8		cartridge[16];
	__u8		reserved304_511[208];
	__be32		old_filemark_list[16680/4];		/* in ADR 1.4 __u8 track_table[16680] */
	os_ext_trk_tb_t	ext_track_tb;
	__u8		reserved17272_17735[464];
	os_fm_tab_t	dat_fm_tab;
	os_fm_tab_t	qfa_fm_tab;
	__u8		reserved25960_32767[6808];
} os_header_t;


/*
 * OnStream ADRL frame
 */
#define OS_FRAME_SIZE   (32 * 1024 + 512)
#define OS_DATA_SIZE    (32 * 1024)
#define OS_AUX_SIZE     (512)
//#define OSST_MAX_SG      2

/* The OnStream tape buffer descriptor. */
struct osst_buffer {
  unsigned char in_use;
  unsigned char dma;	/* DMA-able buffer */
  int buffer_size;
  int buffer_blocks;
  int buffer_bytes;
  int read_pointer;
  int writing;
  int midlevel_result;
  int syscall_result;
  struct osst_request *last_SRpnt;
  struct st_cmdstatus cmdstat;
  unsigned char *b_data;
  os_aux_t *aux;               /* onstream AUX structure at end of each block     */
  unsigned short use_sg;       /* zero or number of s/g segments for this adapter */
  unsigned short sg_segs;      /* number of segments in s/g list                  */
  unsigned short orig_sg_segs; /* number of segments allocated at first try       */
  struct scatterlist sg[1];    /* MUST BE last item                               */
} ;

/* The OnStream tape drive descriptor */
struct osst_tape {
  struct scsi_driver *driver;
  unsigned capacity;
  struct scsi_device *device;
  struct mutex lock;           /* for serialization */
  struct completion wait;      /* for SCSI commands */
  struct osst_buffer * buffer;

  /* Drive characteristics */
  unsigned char omit_blklims;
  unsigned char do_auto_lock;
  unsigned char can_bsr;
  unsigned char can_partitions;
  unsigned char two_fm;
  unsigned char fast_mteom;
  unsigned char restr_dma;
  unsigned char scsi2_logical;
  unsigned char default_drvbuffer;  /* 0xff = don't touch, value 3 bits */
  unsigned char pos_unknown;        /* after reset position unknown */
  int write_threshold;
  int timeout;			/* timeout for normal commands */
  int long_timeout;		/* timeout for commands known to take long time*/

  /* Mode characteristics */
  struct st_modedef modes[ST_NBR_MODES];
  int current_mode;

  /* Status variables */
  int partition;
  int new_partition;
  int nbr_partitions;    /* zero until partition support enabled */
  struct st_partstat ps[ST_NBR_PARTITIONS];
  unsigned char dirty;
  unsigned char ready;
  unsigned char write_prot;
  unsigned char drv_write_prot;
  unsigned char in_use;
  unsigned char blksize_changed;
  unsigned char density_changed;
  unsigned char compression_changed;
  unsigned char drv_buffer;
  unsigned char density;
  unsigned char door_locked;
  unsigned char rew_at_close;
  unsigned char inited;
  int block_size;
  int min_block;
  int max_block;
  int recover_count;            /* from tape opening */
  int abort_count;
  int write_count;
  int read_count;
  int recover_erreg;            /* from last status call */
  /*
   * OnStream specific data
   */
  int	   os_fw_rev;			       /* the firmware revision * 10000 */
  unsigned char  raw;                          /* flag OnStream raw access (32.5KB block size) */
  unsigned char  poll;                         /* flag that this drive needs polling (IDE|firmware) */
  unsigned char  frame_in_buffer;	       /* flag that the frame as per frame_seq_number
						* has been read into STp->buffer and is valid */
  int      frame_seq_number;                   /* logical frame number */
  int      logical_blk_num;                    /* logical block number */
  unsigned first_frame_position;               /* physical frame to be transferred to/from host */
  unsigned last_frame_position;                /* physical frame to be transferd to/from tape */
  int      cur_frames;                         /* current number of frames in internal buffer */
  int      max_frames;                         /* max number of frames in internal buffer */
  char     application_sig[5];                 /* application signature */
  unsigned char  fast_open;                    /* flag that reminds us we didn't check headers at open */
  unsigned short wrt_pass_cntr;                /* write pass counter */
  int      update_frame_cntr;                  /* update frame counter */
  int      onstream_write_error;               /* write error recovery active */
  int      header_ok;                          /* header frame verified ok */
  int      linux_media;                        /* reading linux-specifc media */
  int      linux_media_version;
  os_header_t * header_cache;		       /* cache is kept for filemark positions */
  int      filemark_cnt;
  int      first_mark_ppos;
  int      last_mark_ppos;
  int      last_mark_lbn;			/* storing log_blk_num of last mark is extends ADR spec */
  int      first_data_ppos;
  int      eod_frame_ppos;
  int      eod_frame_lfa;
  int      write_type;				/* used in write error recovery */
  int      read_error_frame;			/* used in read error recovery */
  unsigned long cmd_start_time;
  unsigned long max_cmd_time;

#if DEBUG
  unsigned char write_pending;
  int nbr_finished;
  int nbr_waits;
  unsigned char last_cmnd[6];
  unsigned char last_sense[16];
#endif
  struct gendisk *drive;
} ;

/* scsi tape command */
struct osst_request {
	unsigned char cmd[MAX_COMMAND_SIZE];
	unsigned char sense[SCSI_SENSE_BUFFERSIZE];
	int result;
	struct osst_tape *stp;
	struct completion *waiting;
};

/* Values of write_type */
#define OS_WRITE_DATA      0
#define OS_WRITE_EOD       1
#define OS_WRITE_NEW_MARK  2
#define OS_WRITE_LAST_MARK 3
#define OS_WRITE_HEADER    4
#define OS_WRITE_FILLER    5

/* Additional rw state */
#define OS_WRITING_COMPLETE 3
