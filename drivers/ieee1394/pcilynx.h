#ifndef __PCILYNX_H__
#define __PCILYNX_H__

#include <linux/config.h>

#define PCILYNX_DRIVER_NAME      "pcilynx"
#define PCILYNX_MAJOR            177

#define PCILYNX_MINOR_AUX_START  0
#define PCILYNX_MINOR_ROM_START  16
#define PCILYNX_MINOR_RAM_START  32

#define PCILYNX_MAX_REGISTER     0xfff
#define PCILYNX_MAX_MEMORY       0xffff

#define PCI_DEVICE_ID_TI_PCILYNX 0x8000
#define MAX_PCILYNX_CARDS        4
#define LOCALRAM_SIZE            4096

#define NUM_ISORCV_PCL           4
#define MAX_ISORCV_SIZE          2048
#define ISORCV_PER_PAGE          (PAGE_SIZE / MAX_ISORCV_SIZE)
#define ISORCV_PAGES             (NUM_ISORCV_PCL / ISORCV_PER_PAGE)

#define CHANNEL_LOCALBUS         0
#define CHANNEL_ASYNC_RCV        1
#define CHANNEL_ISO_RCV          2
#define CHANNEL_ASYNC_SEND       3
#define CHANNEL_ISO_SEND         4

#define PCILYNX_CONFIG_ROM_LENGTH   1024

typedef int pcl_t;

struct ti_lynx {
        int id; /* sequential card number */

        spinlock_t lock;

        struct pci_dev *dev;

        struct {
                unsigned reg_1394a:1;
                u32 vendor;
                u32 product;
        } phyic;

        enum { clear, have_intr, have_aux_buf, have_pcl_mem,
               have_1394_buffers, have_iomappings, is_host } state;

        /* remapped memory spaces */
        void __iomem *registers;
        void __iomem *local_rom;
        void __iomem *local_ram;
        void __iomem *aux_port;
	quadlet_t bus_info_block[5];

#ifdef CONFIG_IEEE1394_PCILYNX_PORTS
        atomic_t aux_intr_seen;
        wait_queue_head_t aux_intr_wait;

        void *mem_dma_buffer;
        dma_addr_t mem_dma_buffer_dma;
        struct semaphore mem_dma_mutex;
        wait_queue_head_t mem_dma_intr_wait;
#endif

        /*
         * use local RAM of LOCALRAM_SIZE bytes for PCLs, which allows for
         * LOCALRAM_SIZE * 8 PCLs (each sized 128 bytes);
         * the following is an allocation bitmap
         */
        u8 pcl_bmap[LOCALRAM_SIZE / 1024];

#ifndef CONFIG_IEEE1394_PCILYNX_LOCALRAM
	/* point to PCLs memory area if needed */
	void *pcl_mem;
        dma_addr_t pcl_mem_dma;
#endif

        /* PCLs for local mem / aux transfers */
        pcl_t dmem_pcl;

        /* IEEE-1394 part follows */
        struct hpsb_host *host;

        int phyid, isroot;
        int selfid_size;
        int phy_reg0;

        spinlock_t phy_reg_lock;

        pcl_t rcv_pcl_start, rcv_pcl;
        void *rcv_page;
        dma_addr_t rcv_page_dma;
        int rcv_active;

        struct lynx_send_data {
                pcl_t pcl_start, pcl;
                struct list_head queue;
                struct list_head pcl_queue; /* this queue contains at most one packet */
                spinlock_t queue_lock;
                dma_addr_t header_dma, data_dma;
                int channel;
        } async, iso_send;

        struct {
                pcl_t pcl[NUM_ISORCV_PCL];
                u32 stat[NUM_ISORCV_PCL];
                void *page[ISORCV_PAGES];
                dma_addr_t page_dma[ISORCV_PAGES];
                pcl_t pcl_start;
                int chan_count;
                int next, last, used, running;
                struct tasklet_struct tq;
                spinlock_t lock;
        } iso_rcv;

	u32 i2c_driven_state; /* the state we currently drive the Serial EEPROM Control register */
};

/* the per-file data structure for mem space access */
struct memdata {
        struct ti_lynx *lynx;
        int cid;
        atomic_t aux_intr_last_seen;
	/* enum values are the same as LBUS_ADDR_SEL_* values below */
        enum { rom = 0x10000, aux = 0x20000, ram = 0 } type;
};



/*
 * Register read and write helper functions.
 */
static inline void reg_write(const struct ti_lynx *lynx, int offset, u32 data)
{
        writel(data, lynx->registers + offset);
}

static inline u32 reg_read(const struct ti_lynx *lynx, int offset)
{
        return readl(lynx->registers + offset);
}

static inline void reg_set_bits(const struct ti_lynx *lynx, int offset,
                                u32 mask)
{
        reg_write(lynx, offset, (reg_read(lynx, offset) | mask));
}

static inline void reg_clear_bits(const struct ti_lynx *lynx, int offset,
                                  u32 mask)
{
        reg_write(lynx, offset, (reg_read(lynx, offset) & ~mask));
}



/* chip register definitions follow */

#define PCI_LATENCY_CACHELINE             0x0c

#define MISC_CONTROL                      0x40
#define MISC_CONTROL_SWRESET              (1<<0)

#define SERIAL_EEPROM_CONTROL             0x44

#define PCI_INT_STATUS                    0x48
#define PCI_INT_ENABLE                    0x4c
/* status and enable have identical bit numbers */
#define PCI_INT_INT_PEND                  (1<<31)
#define PCI_INT_FORCED_INT                (1<<30)
#define PCI_INT_SLV_ADR_PERR              (1<<28)
#define PCI_INT_SLV_DAT_PERR              (1<<27)
#define PCI_INT_MST_DAT_PERR              (1<<26)
#define PCI_INT_MST_DEV_TIMEOUT           (1<<25)
#define PCI_INT_INTERNAL_SLV_TIMEOUT      (1<<23)
#define PCI_INT_AUX_TIMEOUT               (1<<18)
#define PCI_INT_AUX_INT                   (1<<17)
#define PCI_INT_1394                      (1<<16)
#define PCI_INT_DMA4_PCL                  (1<<9)
#define PCI_INT_DMA4_HLT                  (1<<8)
#define PCI_INT_DMA3_PCL                  (1<<7)
#define PCI_INT_DMA3_HLT                  (1<<6)
#define PCI_INT_DMA2_PCL                  (1<<5)
#define PCI_INT_DMA2_HLT                  (1<<4)
#define PCI_INT_DMA1_PCL                  (1<<3)
#define PCI_INT_DMA1_HLT                  (1<<2)
#define PCI_INT_DMA0_PCL                  (1<<1)
#define PCI_INT_DMA0_HLT                  (1<<0)
/* all DMA interrupts combined: */
#define PCI_INT_DMA_ALL                   0x3ff

#define PCI_INT_DMA_HLT(chan)             (1 << (chan * 2))
#define PCI_INT_DMA_PCL(chan)             (1 << (chan * 2 + 1))

#define LBUS_ADDR                         0xb4
#define LBUS_ADDR_SEL_RAM                 (0x0<<16)
#define LBUS_ADDR_SEL_ROM                 (0x1<<16)
#define LBUS_ADDR_SEL_AUX                 (0x2<<16)
#define LBUS_ADDR_SEL_ZV                  (0x3<<16)

#define GPIO_CTRL_A                       0xb8
#define GPIO_CTRL_B                       0xbc
#define GPIO_DATA_BASE                    0xc0

#define DMA_BREG(base, chan)              (base + chan * 0x20)
#define DMA_SREG(base, chan)              (base + chan * 0x10)

#define DMA0_PREV_PCL                     0x100
#define DMA1_PREV_PCL                     0x120
#define DMA2_PREV_PCL                     0x140
#define DMA3_PREV_PCL                     0x160
#define DMA4_PREV_PCL                     0x180
#define DMA_PREV_PCL(chan)                (DMA_BREG(DMA0_PREV_PCL, chan))

#define DMA0_CURRENT_PCL                  0x104
#define DMA1_CURRENT_PCL                  0x124
#define DMA2_CURRENT_PCL                  0x144
#define DMA3_CURRENT_PCL                  0x164
#define DMA4_CURRENT_PCL                  0x184
#define DMA_CURRENT_PCL(chan)             (DMA_BREG(DMA0_CURRENT_PCL, chan))

#define DMA0_CHAN_STAT                    0x10c
#define DMA1_CHAN_STAT                    0x12c
#define DMA2_CHAN_STAT                    0x14c
#define DMA3_CHAN_STAT                    0x16c
#define DMA4_CHAN_STAT                    0x18c
#define DMA_CHAN_STAT(chan)               (DMA_BREG(DMA0_CHAN_STAT, chan))
/* CHAN_STATUS registers share bits */
#define DMA_CHAN_STAT_SELFID              (1<<31)
#define DMA_CHAN_STAT_ISOPKT              (1<<30)
#define DMA_CHAN_STAT_PCIERR              (1<<29)
#define DMA_CHAN_STAT_PKTERR              (1<<28)
#define DMA_CHAN_STAT_PKTCMPL             (1<<27)
#define DMA_CHAN_STAT_SPECIALACK          (1<<14)


#define DMA0_CHAN_CTRL                    0x110
#define DMA1_CHAN_CTRL                    0x130
#define DMA2_CHAN_CTRL                    0x150
#define DMA3_CHAN_CTRL                    0x170
#define DMA4_CHAN_CTRL                    0x190
#define DMA_CHAN_CTRL(chan)               (DMA_BREG(DMA0_CHAN_CTRL, chan))
/* CHAN_CTRL registers share bits */
#define DMA_CHAN_CTRL_ENABLE              (1<<31)
#define DMA_CHAN_CTRL_BUSY                (1<<30)
#define DMA_CHAN_CTRL_LINK                (1<<29)

#define DMA0_READY                        0x114
#define DMA1_READY                        0x134
#define DMA2_READY                        0x154
#define DMA3_READY                        0x174
#define DMA4_READY                        0x194
#define DMA_READY(chan)                   (DMA_BREG(DMA0_READY, chan))

#define DMA_GLOBAL_REGISTER               0x908

#define FIFO_SIZES                        0xa00

#define FIFO_CONTROL                      0xa10
#define FIFO_CONTROL_GRF_FLUSH            (1<<4)
#define FIFO_CONTROL_ITF_FLUSH            (1<<3)
#define FIFO_CONTROL_ATF_FLUSH            (1<<2)

#define FIFO_XMIT_THRESHOLD               0xa14

#define DMA0_WORD0_CMP_VALUE              0xb00
#define DMA1_WORD0_CMP_VALUE              0xb10
#define DMA2_WORD0_CMP_VALUE              0xb20
#define DMA3_WORD0_CMP_VALUE              0xb30
#define DMA4_WORD0_CMP_VALUE              0xb40
#define DMA_WORD0_CMP_VALUE(chan)         (DMA_SREG(DMA0_WORD0_CMP_VALUE, chan))

#define DMA0_WORD0_CMP_ENABLE             0xb04
#define DMA1_WORD0_CMP_ENABLE             0xb14
#define DMA2_WORD0_CMP_ENABLE             0xb24
#define DMA3_WORD0_CMP_ENABLE             0xb34
#define DMA4_WORD0_CMP_ENABLE             0xb44
#define DMA_WORD0_CMP_ENABLE(chan)        (DMA_SREG(DMA0_WORD0_CMP_ENABLE,chan))

#define DMA0_WORD1_CMP_VALUE              0xb08
#define DMA1_WORD1_CMP_VALUE              0xb18
#define DMA2_WORD1_CMP_VALUE              0xb28
#define DMA3_WORD1_CMP_VALUE              0xb38
#define DMA4_WORD1_CMP_VALUE              0xb48
#define DMA_WORD1_CMP_VALUE(chan)         (DMA_SREG(DMA0_WORD1_CMP_VALUE, chan))

#define DMA0_WORD1_CMP_ENABLE             0xb0c
#define DMA1_WORD1_CMP_ENABLE             0xb1c
#define DMA2_WORD1_CMP_ENABLE             0xb2c
#define DMA3_WORD1_CMP_ENABLE             0xb3c
#define DMA4_WORD1_CMP_ENABLE             0xb4c
#define DMA_WORD1_CMP_ENABLE(chan)        (DMA_SREG(DMA0_WORD1_CMP_ENABLE,chan))
/* word 1 compare enable flags */
#define DMA_WORD1_CMP_MATCH_OTHERBUS      (1<<15)
#define DMA_WORD1_CMP_MATCH_BROADCAST     (1<<14)
#define DMA_WORD1_CMP_MATCH_BUS_BCAST     (1<<13)
#define DMA_WORD1_CMP_MATCH_LOCAL_NODE    (1<<12)
#define DMA_WORD1_CMP_MATCH_EXACT         (1<<11)
#define DMA_WORD1_CMP_ENABLE_SELF_ID      (1<<10)
#define DMA_WORD1_CMP_ENABLE_MASTER       (1<<8)

#define LINK_ID                           0xf00
#define LINK_ID_BUS(id)                   (id<<22)
#define LINK_ID_NODE(id)                  (id<<16)

#define LINK_CONTROL                      0xf04
#define LINK_CONTROL_BUSY                 (1<<29)
#define LINK_CONTROL_TX_ISO_EN            (1<<26)
#define LINK_CONTROL_RX_ISO_EN            (1<<25)
#define LINK_CONTROL_TX_ASYNC_EN          (1<<24)
#define LINK_CONTROL_RX_ASYNC_EN          (1<<23)
#define LINK_CONTROL_RESET_TX             (1<<21)
#define LINK_CONTROL_RESET_RX             (1<<20)
#define LINK_CONTROL_CYCMASTER            (1<<11)
#define LINK_CONTROL_CYCSOURCE            (1<<10)
#define LINK_CONTROL_CYCTIMEREN           (1<<9)
#define LINK_CONTROL_RCV_CMP_VALID        (1<<7)
#define LINK_CONTROL_SNOOP_ENABLE         (1<<6)

#define CYCLE_TIMER                       0xf08

#define LINK_PHY                          0xf0c
#define LINK_PHY_READ                     (1<<31)
#define LINK_PHY_WRITE                    (1<<30)
#define LINK_PHY_ADDR(addr)               (addr<<24)
#define LINK_PHY_WDATA(data)              (data<<16)
#define LINK_PHY_RADDR(addr)              (addr<<8)


#define LINK_INT_STATUS                   0xf14
#define LINK_INT_ENABLE                   0xf18
/* status and enable have identical bit numbers */
#define LINK_INT_LINK_INT                 (1<<31)
#define LINK_INT_PHY_TIMEOUT              (1<<30)
#define LINK_INT_PHY_REG_RCVD             (1<<29)
#define LINK_INT_PHY_BUSRESET             (1<<28)
#define LINK_INT_TX_RDY                   (1<<26)
#define LINK_INT_RX_DATA_RDY              (1<<25)
#define LINK_INT_ISO_STUCK                (1<<20)
#define LINK_INT_ASYNC_STUCK              (1<<19)
#define LINK_INT_SENT_REJECT              (1<<17)
#define LINK_INT_HDR_ERR                  (1<<16)
#define LINK_INT_TX_INVALID_TC            (1<<15)
#define LINK_INT_CYC_SECOND               (1<<11)
#define LINK_INT_CYC_START                (1<<10)
#define LINK_INT_CYC_DONE                 (1<<9)
#define LINK_INT_CYC_PENDING              (1<<8)
#define LINK_INT_CYC_LOST                 (1<<7)
#define LINK_INT_CYC_ARB_FAILED           (1<<6)
#define LINK_INT_GRF_OVERFLOW             (1<<5)
#define LINK_INT_ITF_UNDERFLOW            (1<<4)
#define LINK_INT_ATF_UNDERFLOW            (1<<3)
#define LINK_INT_ISOARB_FAILED            (1<<0)

/* PHY specifics */
#define PHY_VENDORID_TI                 0x800028
#define PHY_PRODUCTID_TSB41LV03         0x000000


/* this is the physical layout of a PCL, its size is 128 bytes */
struct ti_pcl {
        u32 next;
        u32 async_error_next;
        u32 user_data;
        u32 pcl_status;
        u32 remaining_transfer_count;
        u32 next_data_buffer;
        struct {
                u32 control;
                u32 pointer;
        } buffer[13] __attribute__ ((packed));
} __attribute__ ((packed));

#include <linux/stddef.h>
#define pcloffs(MEMBER) (offsetof(struct ti_pcl, MEMBER))


#ifdef CONFIG_IEEE1394_PCILYNX_LOCALRAM

static inline void put_pcl(const struct ti_lynx *lynx, pcl_t pclid,
                           const struct ti_pcl *pcl)
{
        int i;
        u32 *in = (u32 *)pcl;
        u32 *out = (u32 *)(lynx->local_ram + pclid * sizeof(struct ti_pcl));

        for (i = 0; i < 32; i++, out++, in++) {
                writel(*in, out);
        }
}

static inline void get_pcl(const struct ti_lynx *lynx, pcl_t pclid,
                           struct ti_pcl *pcl)
{
        int i;
        u32 *out = (u32 *)pcl;
        u32 *in = (u32 *)(lynx->local_ram + pclid * sizeof(struct ti_pcl));

        for (i = 0; i < 32; i++, out++, in++) {
                *out = readl(in);
        }
}

static inline u32 pcl_bus(const struct ti_lynx *lynx, pcl_t pclid)
{
        return pci_resource_start(lynx->dev, 1) + pclid * sizeof(struct ti_pcl);
}

#else /* CONFIG_IEEE1394_PCILYNX_LOCALRAM */

static inline void put_pcl(const struct ti_lynx *lynx, pcl_t pclid,
                           const struct ti_pcl *pcl)
{
        memcpy_le32((u32 *)(lynx->pcl_mem + pclid * sizeof(struct ti_pcl)),
                    (u32 *)pcl, sizeof(struct ti_pcl));
}

static inline void get_pcl(const struct ti_lynx *lynx, pcl_t pclid,
                           struct ti_pcl *pcl)
{
        memcpy_le32((u32 *)pcl,
                    (u32 *)(lynx->pcl_mem + pclid * sizeof(struct ti_pcl)),
                    sizeof(struct ti_pcl));
}

static inline u32 pcl_bus(const struct ti_lynx *lynx, pcl_t pclid)
{
        return lynx->pcl_mem_dma + pclid * sizeof(struct ti_pcl);
}

#endif /* CONFIG_IEEE1394_PCILYNX_LOCALRAM */


#if defined (CONFIG_IEEE1394_PCILYNX_LOCALRAM) || defined (__BIG_ENDIAN)
typedef struct ti_pcl pcltmp_t;

static inline struct ti_pcl *edit_pcl(const struct ti_lynx *lynx, pcl_t pclid,
                                      pcltmp_t *tmp)
{
        get_pcl(lynx, pclid, tmp);
        return tmp;
}

static inline void commit_pcl(const struct ti_lynx *lynx, pcl_t pclid,
                              pcltmp_t *tmp)
{
        put_pcl(lynx, pclid, tmp);
}

#else
typedef int pcltmp_t; /* just a dummy */

static inline struct ti_pcl *edit_pcl(const struct ti_lynx *lynx, pcl_t pclid,
                                      pcltmp_t *tmp)
{
        return lynx->pcl_mem + pclid * sizeof(struct ti_pcl);
}

static inline void commit_pcl(const struct ti_lynx *lynx, pcl_t pclid,
                              pcltmp_t *tmp)
{
}
#endif


static inline void run_sub_pcl(const struct ti_lynx *lynx, pcl_t pclid, int idx,
                               int dmachan)
{
        reg_write(lynx, DMA0_CURRENT_PCL + dmachan * 0x20,
                  pcl_bus(lynx, pclid) + idx * 4);
        reg_write(lynx, DMA0_CHAN_CTRL + dmachan * 0x20,
                  DMA_CHAN_CTRL_ENABLE | DMA_CHAN_CTRL_LINK);
}

static inline void run_pcl(const struct ti_lynx *lynx, pcl_t pclid, int dmachan)
{
        run_sub_pcl(lynx, pclid, 0, dmachan);
}

#define PCL_NEXT_INVALID (1<<0)

/* transfer commands */
#define PCL_CMD_RCV            (0x1<<24)
#define PCL_CMD_RCV_AND_UPDATE (0xa<<24)
#define PCL_CMD_XMT            (0x2<<24)
#define PCL_CMD_UNFXMT         (0xc<<24)
#define PCL_CMD_PCI_TO_LBUS    (0x8<<24)
#define PCL_CMD_LBUS_TO_PCI    (0x9<<24)

/* aux commands */
#define PCL_CMD_NOP            (0x0<<24)
#define PCL_CMD_LOAD           (0x3<<24)
#define PCL_CMD_STOREQ         (0x4<<24)
#define PCL_CMD_STORED         (0xb<<24)
#define PCL_CMD_STORE0         (0x5<<24)
#define PCL_CMD_STORE1         (0x6<<24)
#define PCL_CMD_COMPARE        (0xe<<24)
#define PCL_CMD_SWAP_COMPARE   (0xf<<24)
#define PCL_CMD_ADD            (0xd<<24)
#define PCL_CMD_BRANCH         (0x7<<24)

/* BRANCH condition codes */
#define PCL_COND_DMARDY_SET    (0x1<<20)
#define PCL_COND_DMARDY_CLEAR  (0x2<<20)

#define PCL_GEN_INTR           (1<<19)
#define PCL_LAST_BUFF          (1<<18)
#define PCL_LAST_CMD           (PCL_LAST_BUFF)
#define PCL_WAITSTAT           (1<<17)
#define PCL_BIGENDIAN          (1<<16)
#define PCL_ISOMODE            (1<<12)

#endif
