/*******************************************************
*  Define constants                                    *
*                                                      *
********************************************************/

/*-----------------------------------------------------*
*     CCW command codes for CLAW protocol              *
*------------------------------------------------------*/

#define CCW_CLAW_CMD_WRITE           0x01      /* write - not including link */
#define CCW_CLAW_CMD_READ            0x02      /* read */
#define CCW_CLAW_CMD_NOP             0x03      /* NOP */
#define CCW_CLAW_CMD_SENSE           0x04      /* Sense */
#define CCW_CLAW_CMD_SIGNAL_SMOD     0x05      /* Signal Status Modifier */
#define CCW_CLAW_CMD_TIC             0x08      /* TIC */
#define CCW_CLAW_CMD_READHEADER      0x12      /* read header data */
#define CCW_CLAW_CMD_READFF          0x22      /* read an FF */
#define CCW_CLAW_CMD_SENSEID         0xe4      /* Sense ID */


/*-----------------------------------------------------*
*    CLAW Unique constants                             *
*------------------------------------------------------*/

#define MORE_to_COME_FLAG       0x04   /* OR with write CCW in case of m-t-c */
#define CLAW_IDLE               0x00   /* flag to indicate CLAW is idle */
#define CLAW_BUSY               0xff   /* flag to indicate CLAW is busy */
#define CLAW_PENDING            0x00   /* flag to indicate i/o is pending */
#define CLAW_COMPLETE           0xff   /* flag to indicate i/o completed */

/*-----------------------------------------------------*
*     CLAW control command code                        *
*------------------------------------------------------*/

#define SYSTEM_VALIDATE_REQUEST   0x01  /* System Validate request */
#define SYSTEM_VALIDATE_RESPONSE  0x02  /* System Validate response */
#define CONNECTION_REQUEST        0x21  /* Connection request */
#define CONNECTION_RESPONSE       0x22  /* Connection response */
#define CONNECTION_CONFIRM        0x23  /* Connection confirm */
#define DISCONNECT                0x24  /* Disconnect */
#define CLAW_ERROR                0x41  /* CLAW error message */
#define CLAW_VERSION_ID           2     /* CLAW version ID */

/*-----------------------------------------------------*
*  CLAW adater sense bytes                             *
*------------------------------------------------------*/

#define CLAW_ADAPTER_SENSE_BYTE 0x41   /* Stop command issued to adapter */

/*-----------------------------------------------------*
*      CLAW control command return codes               *
*------------------------------------------------------*/

#define CLAW_RC_NAME_MISMATCH       166  /*  names do not match */
#define CLAW_RC_WRONG_VERSION       167  /*  wrong CLAW version number */
#define CLAW_RC_HOST_RCV_TOO_SMALL  180  /*  Host maximum receive is   */
					 /*  less than Linux on zSeries*/
                                         /*  transmit size             */

/*-----------------------------------------------------*
*      CLAW Constants application name                 *
*------------------------------------------------------*/

#define HOST_APPL_NAME          "TCPIP   "
#define WS_APPL_NAME_IP_LINK    "TCPIP   "
#define WS_APPL_NAME_IP_NAME	"IP      "
#define WS_APPL_NAME_API_LINK   "API     "
#define WS_APPL_NAME_PACKED     "PACKED  "
#define WS_NAME_NOT_DEF         "NOT_DEF "
#define PACKING_ASK		1
#define PACK_SEND		2
#define DO_PACKED		3

#define MAX_ENVELOPE_SIZE       65536
#define CLAW_DEFAULT_MTU_SIZE   4096
#define DEF_PACK_BUFSIZE	32768
#define READ_CHANNEL		0
#define WRITE_CHANNEL		1

#define TB_TX                   0          /* sk buffer handling in process  */
#define TB_STOP                 1          /* network device stop in process */
#define TB_RETRY                2          /* retry in process               */
#define TB_NOBUFFER             3          /* no buffer on free queue        */
#define CLAW_MAX_LINK_ID        1
#define CLAW_MAX_DEV            256        /*      max claw devices          */
#define MAX_NAME_LEN            8          /* host name, adapter name length */
#define CLAW_FRAME_SIZE         4096
#define CLAW_ID_SIZE		20+3

/* state machine codes used in claw_irq_handler */

#define CLAW_STOP                0
#define CLAW_START_HALT_IO       1
#define CLAW_START_SENSEID       2
#define CLAW_START_READ          3
#define CLAW_START_WRITE         4

/*-----------------------------------------------------*
*    Lock flag                                         *
*------------------------------------------------------*/
#define LOCK_YES             0
#define LOCK_NO              1

/*-----------------------------------------------------*
*    DBF Debug macros                                  *
*------------------------------------------------------*/
#define CLAW_DBF_TEXT(level, name, text) \
	do { \
		debug_text_event(claw_dbf_##name, level, text); \
	} while (0)

#define CLAW_DBF_HEX(level,name,addr,len) \
do { \
	debug_event(claw_dbf_##name,level,(void*)(addr),len); \
} while (0)

/* Allow to sort out low debug levels early to avoid wasted sprints */
static inline int claw_dbf_passes(debug_info_t *dbf_grp, int level)
{
	return (level <= dbf_grp->level);
}

#define CLAW_DBF_TEXT_(level,name,text...) \
	do { \
		if (claw_dbf_passes(claw_dbf_##name, level)) { \
			sprintf(debug_buffer, text); \
			debug_text_event(claw_dbf_##name, level, \
						debug_buffer); \
		} \
	} while (0)

/**
 * Enum for classifying detected devices.
 */
enum claw_channel_types {
	/* Device is not a channel  */
	claw_channel_type_none,

	/* Device is a CLAW channel device */
	claw_channel_type_claw
};


/*******************************************************
*  Define Control Blocks                               *
*                                                      *
********************************************************/

/*------------------------------------------------------*/
/*     CLAW header                                      */
/*------------------------------------------------------*/

struct clawh {
        __u16  length;     /* length of data read by preceding read CCW */
        __u8   opcode;     /* equivalent read CCW */
        __u8   flag;       /* flag of FF to indicate read was completed */
};

/*------------------------------------------------------*/
/*     CLAW Packing header   4 bytes                    */
/*------------------------------------------------------*/
struct clawph {
       __u16 len;  	/* Length of Packed Data Area   */
       __u8  flag;  	/* Reserved not used            */
       __u8  link_num;	/* Link ID                      */
};

/*------------------------------------------------------*/
/*     CLAW Ending struct ccwbk                         */
/*------------------------------------------------------*/
struct endccw {
	__u32     real;            /* real address of this block */
       __u8      write1;          /* write 1 is active */
        __u8      read1;           /* read 1 is active  */
        __u16     reserved;        /* reserved for future use */
        struct ccw1    write1_nop1;
        struct ccw1    write1_nop2;
        struct ccw1    write2_nop1;
        struct ccw1    write2_nop2;
        struct ccw1    read1_nop1;
        struct ccw1    read1_nop2;
        struct ccw1    read2_nop1;
        struct ccw1    read2_nop2;
};

/*------------------------------------------------------*/
/*     CLAW struct ccwbk                                       */
/*------------------------------------------------------*/
struct ccwbk {
        void   *next;        /* pointer to next ccw block */
        __u32     real;         /* real address of this ccw */
        void      *p_buffer;    /* virtual address of data */
        struct clawh     header;       /* claw header */
        struct ccw1    write;   /* write CCW    */
        struct ccw1    w_read_FF; /* read FF */
        struct ccw1    w_TIC_1;        /* TIC */
        struct ccw1    read;         /* read CCW  */
        struct ccw1    read_h;        /* read header */
        struct ccw1    signal;       /* signal SMOD  */
        struct ccw1    r_TIC_1;        /* TIC1 */
        struct ccw1    r_read_FF;      /* read FF  */
        struct ccw1    r_TIC_2;        /* TIC2 */
};

/*------------------------------------------------------*/
/*     CLAW control block                               */
/*------------------------------------------------------*/
struct clawctl {
        __u8    command;      /* control command */
        __u8    version;      /* CLAW protocol version */
        __u8    linkid;       /* link ID   */
        __u8    correlator;   /* correlator */
        __u8    rc;           /* return code */
        __u8    reserved1;    /* reserved */
        __u8    reserved2;    /* reserved */
        __u8    reserved3;    /* reserved */
        __u8    data[24];     /* command specific fields */
};

/*------------------------------------------------------*/
/*     Data for SYSTEMVALIDATE command                  */
/*------------------------------------------------------*/
struct sysval  {
        char    WS_name[8];        /* Workstation System name  */
        char    host_name[8];      /* Host system name     */
        __u16   read_frame_size;   /* read frame size */
        __u16   write_frame_size;  /* write frame size */
        __u8    reserved[4];       /* reserved */
};

/*------------------------------------------------------*/
/*     Data for Connect command                         */
/*------------------------------------------------------*/
struct conncmd  {
        char     WS_name[8];       /* Workstation application name  */
        char     host_name[8];     /* Host application name      */
        __u16    reserved1[2];     /* read frame size */
        __u8     reserved2[4];     /* reserved  */
};

/*------------------------------------------------------*/
/*     Data for CLAW error                              */
/*------------------------------------------------------*/
struct clawwerror  {
        char      reserved1[8];   /* reserved */
        char      reserved2[8];   /* reserved  */
        char      reserved3[8];   /* reserved  */
};

/*------------------------------------------------------*/
/*     Data buffer for CLAW                             */
/*------------------------------------------------------*/
struct clawbuf  {
       char      buffer[MAX_ENVELOPE_SIZE];   /* data buffer */
};

/*------------------------------------------------------*/
/*     Channel control block for read and write channel */
/*------------------------------------------------------*/

struct chbk {
        unsigned int        devno;
        int                 irq;
	char 		    id[CLAW_ID_SIZE];
       __u32               IO_active;
        __u8                claw_state;
        struct irb          *irb;
       	struct ccw_device   *cdev;  /* pointer to the channel device */
	struct net_device   *ndev;
        wait_queue_head_t   wait;
        struct tasklet_struct    tasklet;
        struct timer_list   timer;
        unsigned long       flag_a;    /* atomic flags */
#define CLAW_BH_ACTIVE      0
        unsigned long       flag_b;    /* atomic flags */
#define CLAW_WRITE_ACTIVE   0
        __u8                last_dstat;
        __u8                flag;
	struct sk_buff_head collect_queue;
	spinlock_t collect_lock;
#define CLAW_WRITE      0x02      /* - Set if this is a write channel */
#define CLAW_READ	0x01      /* - Set if this is a read channel  */
#define CLAW_TIMER      0x80      /* - Set if timer made the wake_up  */
};

/*--------------------------------------------------------------*
*           CLAW  environment block                             *
*---------------------------------------------------------------*/

struct claw_env {
        unsigned int            devno[2];       /* device number */
        char                    host_name[9];   /* Host name */
        char                    adapter_name [9]; /* adapter name */
        char                    api_type[9];    /* TCPIP, API or PACKED */
        void                    *p_priv;        /* privptr */
        __u16                   read_buffers;   /* read buffer number */
        __u16                   write_buffers;  /* write buffer number */
        __u16                   read_size;      /* read buffer size */
        __u16                   write_size;     /* write buffer size */
        __u16                   dev_id;         /* device ident */
	__u8			packing;	/* are we packing? */
        __u8                    in_use;         /* device active flag */
        struct net_device       *ndev;    	/* backward ptr to the net dev*/
};

/*--------------------------------------------------------------*
*           CLAW  main control block                            *
*---------------------------------------------------------------*/

struct claw_privbk {
        void *p_buff_ccw;
        __u32      p_buff_ccw_num;
        void  *p_buff_read;
        __u32      p_buff_read_num;
        __u32      p_buff_pages_perread;
        void  *p_buff_write;
        __u32      p_buff_write_num;
        __u32      p_buff_pages_perwrite;
        long       active_link_ID;           /* Active logical link ID */
        struct ccwbk *p_write_free_chain;     /* pointer to free ccw chain */
        struct ccwbk *p_write_active_first;   /* ptr to the first write ccw */
        struct ccwbk *p_write_active_last;    /* ptr to the last write ccw */
        struct ccwbk *p_read_active_first;    /* ptr to the first read ccw */
        struct ccwbk *p_read_active_last;     /* ptr to the last read ccw */
        struct endccw *p_end_ccw;              /*ptr to ending ccw */
        struct ccwbk *p_claw_signal_blk;      /* ptr to signal block */
        __u32      write_free_count;       /* number of free bufs for write */
	struct     net_device_stats  stats; /* 	 device status    */
        struct chbk channel[2];            /* Channel control blocks */
        __u8       mtc_skipping;
        int        mtc_offset;
        int        mtc_logical_link;
        void       *p_mtc_envelope;
	struct	   sk_buff	*pk_skb;	/* packing buffer    */
	int	   pk_cnt;
        struct clawctl ctl_bk;
        struct claw_env *p_env;
        __u8       system_validate_comp;
        __u8       release_pend;
        __u8      checksum_received_ip_pkts;
	__u8      buffs_alloc;
        struct endccw  end_ccw;
        unsigned long  tbusy;

};


/************************************************************/
/* define global constants                                  */
/************************************************************/

#define CCWBK_SIZE sizeof(struct ccwbk)


