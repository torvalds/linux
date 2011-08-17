#ifndef _FORE200E_H
#define _FORE200E_H

#ifdef __KERNEL__

/* rx buffer sizes */

#define SMALL_BUFFER_SIZE    384     /* size of small buffers (multiple of 48 (PCA) and 64 (SBA) bytes) */
#define LARGE_BUFFER_SIZE    4032    /* size of large buffers (multiple of 48 (PCA) and 64 (SBA) bytes) */


#define RBD_BLK_SIZE	     32      /* nbr of supplied rx buffers per rbd */


#define MAX_PDU_SIZE	     65535   /* maximum PDU size supported by AALs */


#define BUFFER_S1_SIZE       SMALL_BUFFER_SIZE    /* size of small buffers, scheme 1 */
#define BUFFER_L1_SIZE       LARGE_BUFFER_SIZE    /* size of large buffers, scheme 1 */

#define BUFFER_S2_SIZE       SMALL_BUFFER_SIZE    /* size of small buffers, scheme 2 */
#define BUFFER_L2_SIZE       LARGE_BUFFER_SIZE    /* size of large buffers, scheme 2 */

#define BUFFER_S1_NBR        (RBD_BLK_SIZE * 6)
#define BUFFER_L1_NBR        (RBD_BLK_SIZE * 4)

#define BUFFER_S2_NBR        (RBD_BLK_SIZE * 6)
#define BUFFER_L2_NBR        (RBD_BLK_SIZE * 4)


#define QUEUE_SIZE_CMD       16	     /* command queue capacity       */
#define QUEUE_SIZE_RX	     64	     /* receive queue capacity       */
#define QUEUE_SIZE_TX	     256     /* transmit queue capacity      */
#define QUEUE_SIZE_BS        32	     /* buffer supply queue capacity */

#define FORE200E_VPI_BITS     0
#define FORE200E_VCI_BITS    10
#define NBR_CONNECT          (1 << (FORE200E_VPI_BITS + FORE200E_VCI_BITS)) /* number of connections */


#define TSD_FIXED            2
#define TSD_EXTENSION        0
#define TSD_NBR              (TSD_FIXED + TSD_EXTENSION)


/* the cp starts putting a received PDU into one *small* buffer,
   then it uses a number of *large* buffers for the trailing data. 
   we compute here the total number of receive segment descriptors 
   required to hold the largest possible PDU */

#define RSD_REQUIRED  (((MAX_PDU_SIZE - SMALL_BUFFER_SIZE + LARGE_BUFFER_SIZE) / LARGE_BUFFER_SIZE) + 1)

#define RSD_FIXED     3

/* RSD_REQUIRED receive segment descriptors are enough to describe a max-sized PDU,
   but we have to keep the size of the receive PDU descriptor multiple of 32 bytes,
   so we add one extra RSD to RSD_EXTENSION 
   (WARNING: THIS MAY CHANGE IF BUFFER SIZES ARE MODIFIED) */

#define RSD_EXTENSION  ((RSD_REQUIRED - RSD_FIXED) + 1)
#define RSD_NBR         (RSD_FIXED + RSD_EXTENSION)


#define FORE200E_DEV(d)          ((struct fore200e*)((d)->dev_data))
#define FORE200E_VCC(d)          ((struct fore200e_vcc*)((d)->dev_data))

/* bitfields endian games */

#if defined(__LITTLE_ENDIAN_BITFIELD)
#define BITFIELD2(b1, b2)                    b1; b2;
#define BITFIELD3(b1, b2, b3)                b1; b2; b3;
#define BITFIELD4(b1, b2, b3, b4)            b1; b2; b3; b4;
#define BITFIELD5(b1, b2, b3, b4, b5)        b1; b2; b3; b4; b5;
#define BITFIELD6(b1, b2, b3, b4, b5, b6)    b1; b2; b3; b4; b5; b6;
#elif defined(__BIG_ENDIAN_BITFIELD)
#define BITFIELD2(b1, b2)                                    b2; b1;
#define BITFIELD3(b1, b2, b3)                            b3; b2; b1;
#define BITFIELD4(b1, b2, b3, b4)                    b4; b3; b2; b1;
#define BITFIELD5(b1, b2, b3, b4, b5)            b5; b4; b3; b2; b1;
#define BITFIELD6(b1, b2, b3, b4, b5, b6)    b6; b5; b4; b3; b2; b1;
#else
#error unknown bitfield endianess
#endif

 
/* ATM cell header (minus HEC byte) */

typedef struct atm_header {
    BITFIELD5( 
        u32 clp :  1,    /* cell loss priority         */
        u32 plt :  3,    /* payload type               */
        u32 vci : 16,    /* virtual channel identifier */
        u32 vpi :  8,    /* virtual path identifier    */
        u32 gfc :  4     /* generic flow control       */
   )
} atm_header_t;


/* ATM adaptation layer id */

typedef enum fore200e_aal {
    FORE200E_AAL0  = 0,
    FORE200E_AAL34 = 4,
    FORE200E_AAL5  = 5,
} fore200e_aal_t;


/* transmit PDU descriptor specification */

typedef struct tpd_spec {
    BITFIELD4(
        u32               length : 16,    /* total PDU length            */
        u32               nseg   :  8,    /* number of transmit segments */
        enum fore200e_aal aal    :  4,    /* adaptation layer            */
        u32               intr   :  4     /* interrupt requested         */
    )
} tpd_spec_t;


/* transmit PDU rate control */

typedef struct tpd_rate
{
    BITFIELD2( 
        u32 idle_cells : 16,    /* number of idle cells to insert   */
        u32 data_cells : 16     /* number of data cells to transmit */
    )
} tpd_rate_t;


/* transmit segment descriptor */

typedef struct tsd {
    u32 buffer;    /* transmit buffer DMA address */
    u32 length;    /* number of bytes in buffer   */
} tsd_t;


/* transmit PDU descriptor */

typedef struct tpd {
    struct atm_header atm_header;        /* ATM header minus HEC byte    */
    struct tpd_spec   spec;              /* tpd specification            */
    struct tpd_rate   rate;              /* tpd rate control             */
    u32               pad;               /* reserved                     */
    struct tsd        tsd[ TSD_NBR ];    /* transmit segment descriptors */
} tpd_t;


/* receive segment descriptor */

typedef struct rsd {
    u32 handle;    /* host supplied receive buffer handle */
    u32 length;    /* number of bytes in buffer           */
} rsd_t;


/* receive PDU descriptor */

typedef struct rpd {
    struct atm_header atm_header;        /* ATM header minus HEC byte   */
    u32               nseg;              /* number of receive segments  */
    struct rsd        rsd[ RSD_NBR ];    /* receive segment descriptors */
} rpd_t;


/* buffer scheme */

typedef enum buffer_scheme {
    BUFFER_SCHEME_ONE,
    BUFFER_SCHEME_TWO,
    BUFFER_SCHEME_NBR    /* always last */
} buffer_scheme_t;


/* buffer magnitude */

typedef enum buffer_magn {
    BUFFER_MAGN_SMALL,
    BUFFER_MAGN_LARGE,
    BUFFER_MAGN_NBR    /* always last */
} buffer_magn_t;


/* receive buffer descriptor */

typedef struct rbd {
    u32 handle;          /* host supplied handle            */
    u32 buffer_haddr;    /* host DMA address of host buffer */
} rbd_t;


/* receive buffer descriptor block */

typedef struct rbd_block {
    struct rbd rbd[ RBD_BLK_SIZE ];    /* receive buffer descriptor */
} rbd_block_t;


/* tpd DMA address */

typedef struct tpd_haddr {
    BITFIELD3( 
        u32 size  :  4,    /* tpd size expressed in 32 byte blocks     */
        u32 pad   :  1,    /* reserved                                 */
        u32 haddr : 27     /* tpd DMA addr aligned on 32 byte boundary */
    )
} tpd_haddr_t;

#define TPD_HADDR_SHIFT 5  /* addr aligned on 32 byte boundary */

/* cp resident transmit queue entry */

typedef struct cp_txq_entry {
    struct tpd_haddr tpd_haddr;       /* host DMA address of tpd                */
    u32              status_haddr;    /* host DMA address of completion status  */
} cp_txq_entry_t;


/* cp resident receive queue entry */

typedef struct cp_rxq_entry {
    u32 rpd_haddr;       /* host DMA address of rpd                */
    u32 status_haddr;    /* host DMA address of completion status  */
} cp_rxq_entry_t;


/* cp resident buffer supply queue entry */

typedef struct cp_bsq_entry {
    u32 rbd_block_haddr;    /* host DMA address of rbd block          */
    u32 status_haddr;       /* host DMA address of completion status  */
} cp_bsq_entry_t;


/* completion status */

typedef volatile enum status {
    STATUS_PENDING  = (1<<0),    /* initial status (written by host)  */
    STATUS_COMPLETE = (1<<1),    /* completion status (written by cp) */
    STATUS_FREE     = (1<<2),    /* initial status (written by host)  */
    STATUS_ERROR    = (1<<3)     /* completion status (written by cp) */
} status_t;


/* cp operation code */

typedef enum opcode {
    OPCODE_INITIALIZE = 1,          /* initialize board                       */
    OPCODE_ACTIVATE_VCIN,           /* activate incoming VCI                  */
    OPCODE_ACTIVATE_VCOUT,          /* activate outgoing VCI                  */
    OPCODE_DEACTIVATE_VCIN,         /* deactivate incoming VCI                */
    OPCODE_DEACTIVATE_VCOUT,        /* deactivate incoing VCI                 */
    OPCODE_GET_STATS,               /* get board statistics                   */
    OPCODE_SET_OC3,                 /* set OC-3 registers                     */
    OPCODE_GET_OC3,                 /* get OC-3 registers                     */
    OPCODE_RESET_STATS,             /* reset board statistics                 */
    OPCODE_GET_PROM,                /* get expansion PROM data (PCI specific) */
    OPCODE_SET_VPI_BITS,            /* set x bits of those decoded by the
				       firmware to be low order bits from
				       the VPI field of the ATM cell header   */
    OPCODE_REQUEST_INTR = (1<<7)    /* request interrupt                      */
} opcode_t;


/* virtual path / virtual channel identifiers */

typedef struct vpvc {
    BITFIELD3(
        u32 vci : 16,    /* virtual channel identifier */
        u32 vpi :  8,    /* virtual path identifier    */
        u32 pad :  8     /* reserved                   */
    )
} vpvc_t;


/* activate VC command opcode */

typedef struct activate_opcode {
    BITFIELD4( 
        enum opcode        opcode : 8,    /* cp opcode        */
        enum fore200e_aal  aal    : 8,    /* adaptation layer */
        enum buffer_scheme scheme : 8,    /* buffer scheme    */
        u32  pad                  : 8     /* reserved         */
   )
} activate_opcode_t;


/* activate VC command block */

typedef struct activate_block {
    struct activate_opcode  opcode;    /* activate VC command opcode */
    struct vpvc             vpvc;      /* VPI/VCI                    */
    u32                     mtu;       /* for AAL0 only              */

} activate_block_t;


/* deactivate VC command opcode */

typedef struct deactivate_opcode {
    BITFIELD2(
        enum opcode opcode :  8,    /* cp opcode */
        u32         pad    : 24     /* reserved  */
    )
} deactivate_opcode_t;


/* deactivate VC command block */

typedef struct deactivate_block {
    struct deactivate_opcode opcode;    /* deactivate VC command opcode */
    struct vpvc              vpvc;      /* VPI/VCI                      */
} deactivate_block_t;


/* OC-3 registers */

typedef struct oc3_regs {
    u32 reg[ 128 ];    /* see the PMC Sierra PC5346 S/UNI-155-Lite
			  Saturn User Network Interface documentation
			  for a description of the OC-3 chip registers */
} oc3_regs_t;


/* set/get OC-3 regs command opcode */

typedef struct oc3_opcode {
    BITFIELD4(
        enum opcode opcode : 8,    /* cp opcode                           */
	u32         reg    : 8,    /* register index                      */
	u32         value  : 8,    /* register value                      */
	u32         mask   : 8     /* register mask that specifies which
				      bits of the register value field
				      are significant                     */
    )
} oc3_opcode_t;


/* set/get OC-3 regs command block */

typedef struct oc3_block {
    struct oc3_opcode opcode;        /* set/get OC-3 regs command opcode     */
    u32               regs_haddr;    /* host DMA address of OC-3 regs buffer */
} oc3_block_t;


/* physical encoding statistics */

typedef struct stats_phy {
    __be32 crc_header_errors;    /* cells received with bad header CRC */
    __be32 framing_errors;       /* cells received with bad framing    */
    __be32 pad[ 2 ];             /* i960 padding                       */
} stats_phy_t;


/* OC-3 statistics */

typedef struct stats_oc3 {
    __be32 section_bip8_errors;    /* section 8 bit interleaved parity    */
    __be32 path_bip8_errors;       /* path 8 bit interleaved parity       */
    __be32 line_bip24_errors;      /* line 24 bit interleaved parity      */
    __be32 line_febe_errors;       /* line far end block errors           */
    __be32 path_febe_errors;       /* path far end block errors           */
    __be32 corr_hcs_errors;        /* correctable header check sequence   */
    __be32 ucorr_hcs_errors;       /* uncorrectable header check sequence */
    __be32 pad[ 1 ];               /* i960 padding                        */
} stats_oc3_t;


/* ATM statistics */

typedef struct stats_atm {
    __be32	cells_transmitted;    /* cells transmitted                 */
    __be32	cells_received;       /* cells received                    */
    __be32	vpi_bad_range;        /* cell drops: VPI out of range      */
    __be32	vpi_no_conn;          /* cell drops: no connection for VPI */
    __be32	vci_bad_range;        /* cell drops: VCI out of range      */
    __be32	vci_no_conn;          /* cell drops: no connection for VCI */
    __be32	pad[ 2 ];             /* i960 padding                      */
} stats_atm_t;

/* AAL0 statistics */

typedef struct stats_aal0 {
    __be32	cells_transmitted;    /* cells transmitted */
    __be32	cells_received;       /* cells received    */
    __be32	cells_dropped;        /* cells dropped     */
    __be32	pad[ 1 ];             /* i960 padding      */
} stats_aal0_t;


/* AAL3/4 statistics */

typedef struct stats_aal34 {
    __be32	cells_transmitted;         /* cells transmitted from segmented PDUs */
    __be32	cells_received;            /* cells reassembled into PDUs           */
    __be32	cells_crc_errors;          /* payload CRC error count               */
    __be32	cells_protocol_errors;     /* SAR or CS layer protocol errors       */
    __be32	cells_dropped;             /* cells dropped: partial reassembly     */
    __be32	cspdus_transmitted;        /* CS PDUs transmitted                   */
    __be32	cspdus_received;           /* CS PDUs received                      */
    __be32	cspdus_protocol_errors;    /* CS layer protocol errors              */
    __be32	cspdus_dropped;            /* reassembled PDUs drop'd (in cells)    */
    __be32	pad[ 3 ];                  /* i960 padding                          */
} stats_aal34_t;


/* AAL5 statistics */

typedef struct stats_aal5 {
    __be32	cells_transmitted;         /* cells transmitted from segmented SDUs */
    __be32	cells_received;		   /* cells reassembled into SDUs           */
    __be32	cells_dropped;		   /* reassembled PDUs dropped (in cells)   */
    __be32	congestion_experienced;    /* CRC error and length wrong            */
    __be32	cspdus_transmitted;        /* CS PDUs transmitted                   */
    __be32	cspdus_received;           /* CS PDUs received                      */
    __be32	cspdus_crc_errors;         /* CS PDUs CRC errors                    */
    __be32	cspdus_protocol_errors;    /* CS layer protocol errors              */
    __be32	cspdus_dropped;            /* reassembled PDUs dropped              */
    __be32	pad[ 3 ];                  /* i960 padding                          */
} stats_aal5_t;


/* auxiliary statistics */

typedef struct stats_aux {
    __be32	small_b1_failed;     /* receive BD allocation failures  */
    __be32	large_b1_failed;     /* receive BD allocation failures  */
    __be32	small_b2_failed;     /* receive BD allocation failures  */
    __be32	large_b2_failed;     /* receive BD allocation failures  */
    __be32	rpd_alloc_failed;    /* receive PDU allocation failures */
    __be32	receive_carrier;     /* no carrier = 0, carrier = 1     */
    __be32	pad[ 2 ];            /* i960 padding                    */
} stats_aux_t;


/* whole statistics buffer */

typedef struct stats {
    struct stats_phy   phy;      /* physical encoding statistics */
    struct stats_oc3   oc3;      /* OC-3 statistics              */
    struct stats_atm   atm;      /* ATM statistics               */
    struct stats_aal0  aal0;     /* AAL0 statistics              */
    struct stats_aal34 aal34;    /* AAL3/4 statistics            */
    struct stats_aal5  aal5;     /* AAL5 statistics              */
    struct stats_aux   aux;      /* auxiliary statistics         */
} stats_t;


/* get statistics command opcode */

typedef struct stats_opcode {
    BITFIELD2(
        enum opcode opcode :  8,    /* cp opcode */
        u32         pad    : 24     /* reserved  */
    )
} stats_opcode_t;


/* get statistics command block */

typedef struct stats_block {
    struct stats_opcode opcode;         /* get statistics command opcode    */
    u32                 stats_haddr;    /* host DMA address of stats buffer */
} stats_block_t;


/* expansion PROM data (PCI specific) */

typedef struct prom_data {
    u32 hw_revision;      /* hardware revision   */
    u32 serial_number;    /* board serial number */
    u8  mac_addr[ 8 ];    /* board MAC address   */
} prom_data_t;


/* get expansion PROM data command opcode */

typedef struct prom_opcode {
    BITFIELD2(
        enum opcode opcode :  8,    /* cp opcode */
        u32         pad    : 24     /* reserved  */
    )
} prom_opcode_t;


/* get expansion PROM data command block */

typedef struct prom_block {
    struct prom_opcode opcode;        /* get PROM data command opcode    */
    u32                prom_haddr;    /* host DMA address of PROM buffer */
} prom_block_t;


/* cp command */

typedef union cmd {
    enum   opcode           opcode;           /* operation code          */
    struct activate_block   activate_block;   /* activate VC             */
    struct deactivate_block deactivate_block; /* deactivate VC           */
    struct stats_block      stats_block;      /* get statistics          */
    struct prom_block       prom_block;       /* get expansion PROM data */
    struct oc3_block        oc3_block;        /* get/set OC-3 registers  */
    u32                     pad[ 4 ];         /* i960 padding            */
} cmd_t;


/* cp resident command queue */

typedef struct cp_cmdq_entry {
    union cmd cmd;             /* command                               */
    u32       status_haddr;    /* host DMA address of completion status */
    u32       pad[ 3 ];        /* i960 padding                          */
} cp_cmdq_entry_t;


/* host resident transmit queue entry */

typedef struct host_txq_entry {
    struct cp_txq_entry __iomem *cp_entry;    /* addr of cp resident tx queue entry       */
    enum   status*          status;      /* addr of host resident status             */
    struct tpd*             tpd;         /* addr of transmit PDU descriptor          */
    u32                     tpd_dma;     /* DMA address of tpd                       */
    struct sk_buff*         skb;         /* related skb                              */
    void*                   data;        /* copy of misaligned data                  */
    unsigned long           incarn;      /* vc_map incarnation when submitted for tx */
    struct fore200e_vc_map* vc_map;

} host_txq_entry_t;


/* host resident receive queue entry */

typedef struct host_rxq_entry {
    struct cp_rxq_entry __iomem *cp_entry;    /* addr of cp resident rx queue entry */
    enum   status*       status;      /* addr of host resident status       */
    struct rpd*          rpd;         /* addr of receive PDU descriptor     */
    u32                  rpd_dma;     /* DMA address of rpd                 */
} host_rxq_entry_t;


/* host resident buffer supply queue entry */

typedef struct host_bsq_entry {
    struct cp_bsq_entry __iomem *cp_entry;         /* addr of cp resident buffer supply queue entry */
    enum   status*       status;           /* addr of host resident status                  */
    struct rbd_block*    rbd_block;        /* addr of receive buffer descriptor block       */
    u32                  rbd_block_dma;    /* DMA address od rdb                            */
} host_bsq_entry_t;


/* host resident command queue entry */

typedef struct host_cmdq_entry {
    struct cp_cmdq_entry __iomem *cp_entry;    /* addr of cp resident cmd queue entry */
    enum status *status;	       /* addr of host resident status        */
} host_cmdq_entry_t;


/* chunk of memory */

typedef struct chunk {
    void* alloc_addr;    /* base address of allocated chunk */
    void* align_addr;    /* base address of aligned chunk   */
    dma_addr_t dma_addr; /* DMA address of aligned chunk    */
    int   direction;     /* direction of DMA mapping        */
    u32   alloc_size;    /* length of allocated chunk       */
    u32   align_size;    /* length of aligned chunk         */
} chunk_t;

#define dma_size align_size             /* DMA useable size */


/* host resident receive buffer */

typedef struct buffer {
    struct buffer*       next;        /* next receive buffer     */
    enum   buffer_scheme scheme;      /* buffer scheme           */
    enum   buffer_magn   magn;        /* buffer magnitude        */
    struct chunk         data;        /* data buffer             */
#ifdef FORE200E_BSQ_DEBUG
    unsigned long        index;       /* buffer # in queue       */
    int                  supplied;    /* 'buffer supplied' flag  */
#endif
} buffer_t;


#if (BITS_PER_LONG == 32)
#define FORE200E_BUF2HDL(buffer)    ((u32)(buffer))
#define FORE200E_HDL2BUF(handle)    ((struct buffer*)(handle))
#else   /* deal with 64 bit pointers */
#define FORE200E_BUF2HDL(buffer)    ((u32)((u64)(buffer)))
#define FORE200E_HDL2BUF(handle)    ((struct buffer*)(((u64)(handle)) | PAGE_OFFSET))
#endif


/* host resident command queue */

typedef struct host_cmdq {
    struct host_cmdq_entry host_entry[ QUEUE_SIZE_CMD ];    /* host resident cmd queue entries        */
    int                    head;                            /* head of cmd queue                      */
    struct chunk           status;                          /* array of completion status      */
} host_cmdq_t;


/* host resident transmit queue */

typedef struct host_txq {
    struct host_txq_entry host_entry[ QUEUE_SIZE_TX ];    /* host resident tx queue entries         */
    int                   head;                           /* head of tx queue                       */
    int                   tail;                           /* tail of tx queue                       */
    struct chunk          tpd;                            /* array of tpds                          */
    struct chunk          status;                         /* arry of completion status              */
    int                   txing;                          /* number of pending PDUs in tx queue     */
} host_txq_t;


/* host resident receive queue */

typedef struct host_rxq {
    struct host_rxq_entry  host_entry[ QUEUE_SIZE_RX ];    /* host resident rx queue entries         */
    int                    head;                           /* head of rx queue                       */
    struct chunk           rpd;                            /* array of rpds                          */
    struct chunk           status;                         /* array of completion status             */
} host_rxq_t;


/* host resident buffer supply queues */

typedef struct host_bsq {
    struct host_bsq_entry host_entry[ QUEUE_SIZE_BS ];    /* host resident buffer supply queue entries */
    int                   head;                           /* head of buffer supply queue               */
    struct chunk          rbd_block;                      /* array of rbds                             */
    struct chunk          status;                         /* array of completion status                */
    struct buffer*        buffer;                         /* array of rx buffers                       */
    struct buffer*        freebuf;                        /* list of free rx buffers                   */
    volatile int          freebuf_count;                  /* count of free rx buffers                  */
} host_bsq_t;


/* header of the firmware image */

typedef struct fw_header {
    __le32 magic;           /* magic number                               */
    __le32 version;         /* firmware version id                        */
    __le32 load_offset;     /* fw load offset in board memory             */
    __le32 start_offset;    /* fw execution start address in board memory */
} fw_header_t;

#define FW_HEADER_MAGIC  0x65726f66    /* 'fore' */


/* receive buffer supply queues scheme specification */

typedef struct bs_spec {
    u32	queue_length;      /* queue capacity                     */
    u32	buffer_size;	   /* host buffer size			 */
    u32	pool_size;	   /* number of rbds			 */
    u32	supply_blksize;    /* num of rbds in I/O block (multiple
			      of 4 between 4 and 124 inclusive)	 */
} bs_spec_t;


/* initialization command block (one-time command, not in cmd queue) */

typedef struct init_block {
    enum opcode  opcode;               /* initialize command             */
    enum status	 status;	       /* related status word            */
    u32          receive_threshold;    /* not used                       */
    u32          num_connect;          /* ATM connections                */
    u32          cmd_queue_len;        /* length of command queue        */
    u32          tx_queue_len;         /* length of transmit queue       */
    u32          rx_queue_len;         /* length of receive queue        */
    u32          rsd_extension;        /* number of extra 32 byte blocks */
    u32          tsd_extension;        /* number of extra 32 byte blocks */
    u32          conless_vpvc;         /* not used                       */
    u32          pad[ 2 ];             /* force quad alignment           */
    struct bs_spec bs_spec[ BUFFER_SCHEME_NBR ][ BUFFER_MAGN_NBR ];      /* buffer supply queues spec */
} init_block_t;


typedef enum media_type {
    MEDIA_TYPE_CAT5_UTP  = 0x06,    /* unshielded twisted pair */
    MEDIA_TYPE_MM_OC3_ST = 0x16,    /* multimode fiber ST      */
    MEDIA_TYPE_MM_OC3_SC = 0x26,    /* multimode fiber SC      */
    MEDIA_TYPE_SM_OC3_ST = 0x36,    /* single-mode fiber ST    */
    MEDIA_TYPE_SM_OC3_SC = 0x46     /* single-mode fiber SC    */
} media_type_t;

#define FORE200E_MEDIA_INDEX(media_type)   ((media_type)>>4)


/* cp resident queues */

typedef struct cp_queues {
    u32	              cp_cmdq;         /* command queue                      */
    u32	              cp_txq;          /* transmit queue                     */
    u32	              cp_rxq;          /* receive queue                      */
    u32               cp_bsq[ BUFFER_SCHEME_NBR ][ BUFFER_MAGN_NBR ];        /* buffer supply queues */
    u32	              imask;             /* 1 enables cp to host interrupts  */
    u32	              istat;             /* 1 for interrupt posted           */
    u32	              heap_base;         /* offset form beginning of ram     */
    u32	              heap_size;         /* space available for queues       */
    u32	              hlogger;           /* non zero for host logging        */
    u32               heartbeat;         /* cp heartbeat                     */
    u32	              fw_release;        /* firmware version                 */
    u32	              mon960_release;    /* i960 monitor version             */
    u32	              tq_plen;           /* transmit throughput measurements */
    /* make sure the init block remains on a quad word boundary              */
    struct init_block init;              /* one time cmd, not in cmd queue   */
    enum   media_type media_type;        /* media type id                    */
    u32               oc3_revision;      /* OC-3 revision number             */
} cp_queues_t;


/* boot status */

typedef enum boot_status {
    BSTAT_COLD_START    = (u32) 0xc01dc01d,    /* cold start              */
    BSTAT_SELFTEST_OK   = (u32) 0x02201958,    /* self-test ok            */
    BSTAT_SELFTEST_FAIL = (u32) 0xadbadbad,    /* self-test failed        */
    BSTAT_CP_RUNNING    = (u32) 0xce11feed,    /* cp is running           */
    BSTAT_MON_TOO_BIG   = (u32) 0x10aded00     /* i960 monitor is too big */
} boot_status_t;


/* software UART */

typedef struct soft_uart {
    u32 send;    /* write register */
    u32 recv;    /* read register  */
} soft_uart_t;

#define FORE200E_CP_MONITOR_UART_FREE     0x00000000
#define FORE200E_CP_MONITOR_UART_AVAIL    0x01000000


/* i960 monitor */

typedef struct cp_monitor {
    struct soft_uart    soft_uart;      /* software UART           */
    enum boot_status	bstat;          /* boot status             */
    u32			app_base;       /* application base offset */
    u32			mon_version;    /* i960 monitor version    */
} cp_monitor_t;


/* device state */

typedef enum fore200e_state {
    FORE200E_STATE_BLANK,         /* initial state                     */
    FORE200E_STATE_REGISTER,      /* device registered                 */
    FORE200E_STATE_CONFIGURE,     /* bus interface configured          */
    FORE200E_STATE_MAP,           /* board space mapped in host memory */
    FORE200E_STATE_RESET,         /* board resetted                    */
    FORE200E_STATE_START_FW,      /* firmware started                  */
    FORE200E_STATE_INITIALIZE,    /* initialize command successful     */
    FORE200E_STATE_INIT_CMDQ,     /* command queue initialized         */
    FORE200E_STATE_INIT_TXQ,      /* transmit queue initialized        */
    FORE200E_STATE_INIT_RXQ,      /* receive queue initialized         */
    FORE200E_STATE_INIT_BSQ,      /* buffer supply queue initialized   */
    FORE200E_STATE_ALLOC_BUF,     /* receive buffers allocated         */
    FORE200E_STATE_IRQ,           /* host interrupt requested          */
    FORE200E_STATE_COMPLETE       /* initialization completed          */
} fore200e_state;


/* PCA-200E registers */

typedef struct fore200e_pca_regs {
    volatile u32 __iomem * hcr;    /* address of host control register        */
    volatile u32 __iomem * imr;    /* address of host interrupt mask register */
    volatile u32 __iomem * psr;    /* address of PCI specific register        */
} fore200e_pca_regs_t;


/* SBA-200E registers */

typedef struct fore200e_sba_regs {
    u32 __iomem *hcr;    /* address of host control register              */
    u32 __iomem *bsr;    /* address of burst transfer size register       */
    u32 __iomem *isr;    /* address of interrupt level selection register */
} fore200e_sba_regs_t;


/* model-specific registers */

typedef union fore200e_regs {
    struct fore200e_pca_regs pca;    /* PCA-200E registers */
    struct fore200e_sba_regs sba;    /* SBA-200E registers */
} fore200e_regs;


struct fore200e;

/* bus-dependent data */

typedef struct fore200e_bus {
    char*                model_name;          /* board model name                       */
    char*                proc_name;           /* board name under /proc/atm             */
    int                  descr_alignment;     /* tpd/rpd/rbd DMA alignment requirement  */
    int                  buffer_alignment;    /* rx buffers DMA alignment requirement   */
    int                  status_alignment;    /* status words DMA alignment requirement */
    u32                  (*read)(volatile u32 __iomem *);
    void                 (*write)(u32, volatile u32 __iomem *);
    u32                  (*dma_map)(struct fore200e*, void*, int, int);
    void                 (*dma_unmap)(struct fore200e*, u32, int, int);
    void                 (*dma_sync_for_cpu)(struct fore200e*, u32, int, int);
    void                 (*dma_sync_for_device)(struct fore200e*, u32, int, int);
    int                  (*dma_chunk_alloc)(struct fore200e*, struct chunk*, int, int, int);
    void                 (*dma_chunk_free)(struct fore200e*, struct chunk*);
    int                  (*configure)(struct fore200e*); 
    int                  (*map)(struct fore200e*); 
    void                 (*reset)(struct fore200e*);
    int                  (*prom_read)(struct fore200e*, struct prom_data*);
    void                 (*unmap)(struct fore200e*);
    void                 (*irq_enable)(struct fore200e*);
    int                  (*irq_check)(struct fore200e*);
    void                 (*irq_ack)(struct fore200e*);
    int                  (*proc_read)(struct fore200e*, char*);
} fore200e_bus_t;

/* vc mapping */

typedef struct fore200e_vc_map {
    struct atm_vcc* vcc;       /* vcc entry              */
    unsigned long   incarn;    /* vcc incarnation number */
} fore200e_vc_map_t;

#define FORE200E_VC_MAP(fore200e, vpi, vci)  \
        (& (fore200e)->vc_map[ ((vpi) << FORE200E_VCI_BITS) | (vci) ])


/* per-device data */

typedef struct fore200e {
    struct       list_head     entry;                  /* next device                        */
    const struct fore200e_bus* bus;                    /* bus-dependent code and data        */
    union        fore200e_regs regs;                   /* bus-dependent registers            */
    struct       atm_dev*      atm_dev;                /* ATM device                         */

    enum fore200e_state        state;                  /* device state                       */

    char                       name[16];               /* device name                        */
    void*                      bus_dev;                /* bus-specific kernel data           */
    int                        irq;                    /* irq number                         */
    unsigned long              phys_base;              /* physical base address              */
    void __iomem *             virt_base;              /* virtual base address               */
    
    unsigned char              esi[ ESI_LEN ];         /* end system identifier              */

    struct cp_monitor __iomem *         cp_monitor;    /* i960 monitor address               */
    struct cp_queues __iomem *          cp_queues;              /* cp resident queues                 */
    struct host_cmdq           host_cmdq;              /* host resident cmd queue            */
    struct host_txq            host_txq;               /* host resident tx queue             */
    struct host_rxq            host_rxq;               /* host resident rx queue             */
                                                       /* host resident buffer supply queues */
    struct host_bsq            host_bsq[ BUFFER_SCHEME_NBR ][ BUFFER_MAGN_NBR ];       

    u32                        available_cell_rate;    /* remaining pseudo-CBR bw on link    */

    int                        loop_mode;              /* S/UNI loopback mode                */

    struct stats*              stats;                  /* last snapshot of the stats         */
    
    struct mutex               rate_mtx;               /* protects rate reservation ops      */
    spinlock_t                 q_lock;                 /* protects queue ops                 */
#ifdef FORE200E_USE_TASKLET
    struct tasklet_struct      tx_tasklet;             /* performs tx interrupt work         */
    struct tasklet_struct      rx_tasklet;             /* performs rx interrupt work         */
#endif
    unsigned long              tx_sat;                 /* tx queue saturation count          */

    unsigned long              incarn_count;
    struct fore200e_vc_map     vc_map[ NBR_CONNECT ];  /* vc mapping                         */
} fore200e_t;


/* per-vcc data */

typedef struct fore200e_vcc {
    enum buffer_scheme     scheme;             /* rx buffer scheme                   */
    struct tpd_rate        rate;               /* tx rate control data               */
    int                    rx_min_pdu;         /* size of smallest PDU received      */
    int                    rx_max_pdu;         /* size of largest PDU received       */
    int                    tx_min_pdu;         /* size of smallest PDU transmitted   */
    int                    tx_max_pdu;         /* size of largest PDU transmitted    */
    unsigned long          tx_pdu;             /* nbr of tx pdus                     */
    unsigned long          rx_pdu;             /* nbr of rx pdus                     */
} fore200e_vcc_t;



/* 200E-series common memory layout */

#define FORE200E_CP_MONITOR_OFFSET	0x00000400    /* i960 monitor interface */
#define FORE200E_CP_QUEUES_OFFSET	0x00004d40    /* cp resident queues     */


/* PCA-200E memory layout */

#define PCA200E_IOSPACE_LENGTH	        0x00200000

#define PCA200E_HCR_OFFSET		0x00100000    /* board control register */
#define PCA200E_IMR_OFFSET		0x00100004    /* host IRQ mask register */
#define PCA200E_PSR_OFFSET		0x00100008    /* PCI specific register  */


/* PCA-200E host control register */

#define PCA200E_HCR_RESET     (1<<0)    /* read / write */
#define PCA200E_HCR_HOLD_LOCK (1<<1)    /* read / write */
#define PCA200E_HCR_I960FAIL  (1<<2)    /* read         */
#define PCA200E_HCR_INTRB     (1<<2)    /* write        */
#define PCA200E_HCR_HOLD_ACK  (1<<3)    /* read         */
#define PCA200E_HCR_INTRA     (1<<3)    /* write        */
#define PCA200E_HCR_OUTFULL   (1<<4)    /* read         */
#define PCA200E_HCR_CLRINTR   (1<<4)    /* write        */
#define PCA200E_HCR_ESPHOLD   (1<<5)    /* read         */
#define PCA200E_HCR_INFULL    (1<<6)    /* read         */
#define PCA200E_HCR_TESTMODE  (1<<7)    /* read         */


/* PCA-200E PCI bus interface regs (offsets in PCI config space) */

#define PCA200E_PCI_LATENCY      0x40    /* maximum slave latenty            */
#define PCA200E_PCI_MASTER_CTRL  0x41    /* master control                   */
#define PCA200E_PCI_THRESHOLD    0x42    /* burst / continuous req threshold  */

/* PBI master control register */

#define PCA200E_CTRL_DIS_CACHE_RD      (1<<0)    /* disable cache-line reads                         */
#define PCA200E_CTRL_DIS_WRT_INVAL     (1<<1)    /* disable writes and invalidates                   */
#define PCA200E_CTRL_2_CACHE_WRT_INVAL (1<<2)    /* require 2 cache-lines for writes and invalidates */
#define PCA200E_CTRL_IGN_LAT_TIMER     (1<<3)    /* ignore the latency timer                         */
#define PCA200E_CTRL_ENA_CONT_REQ_MODE (1<<4)    /* enable continuous request mode                   */
#define PCA200E_CTRL_LARGE_PCI_BURSTS  (1<<5)    /* force large PCI bus bursts                       */
#define PCA200E_CTRL_CONVERT_ENDIAN    (1<<6)    /* convert endianess of slave RAM accesses          */



#define SBA200E_PROM_NAME  "FORE,sba-200e"    /* device name in openprom tree */


/* size of SBA-200E registers */

#define SBA200E_HCR_LENGTH        4
#define SBA200E_BSR_LENGTH        4
#define SBA200E_ISR_LENGTH        4
#define SBA200E_RAM_LENGTH  0x40000


/* SBA-200E SBUS burst transfer size register */

#define SBA200E_BSR_BURST4   0x04
#define SBA200E_BSR_BURST8   0x08
#define SBA200E_BSR_BURST16  0x10


/* SBA-200E host control register */

#define SBA200E_HCR_RESET        (1<<0)    /* read / write (sticky) */
#define SBA200E_HCR_HOLD_LOCK    (1<<1)    /* read / write (sticky) */
#define SBA200E_HCR_I960FAIL     (1<<2)    /* read                  */
#define SBA200E_HCR_I960SETINTR  (1<<2)    /* write                 */
#define SBA200E_HCR_OUTFULL      (1<<3)    /* read                  */
#define SBA200E_HCR_INTR_CLR     (1<<3)    /* write                 */
#define SBA200E_HCR_INTR_ENA     (1<<4)    /* read / write (sticky) */
#define SBA200E_HCR_ESPHOLD      (1<<5)    /* read                  */
#define SBA200E_HCR_INFULL       (1<<6)    /* read                  */
#define SBA200E_HCR_TESTMODE     (1<<7)    /* read                  */
#define SBA200E_HCR_INTR_REQ     (1<<8)    /* read                  */

#define SBA200E_HCR_STICKY       (SBA200E_HCR_RESET | SBA200E_HCR_HOLD_LOCK | SBA200E_HCR_INTR_ENA)


#endif /* __KERNEL__ */
#endif /* _FORE200E_H */
