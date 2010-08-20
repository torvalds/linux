/*
 * Chip register definitions for PCILynx chipset.  Based on pcilynx.h
 * from the Linux 1394 drivers, but modified a bit so the names here
 * match the specification exactly (even though they have weird names,
 * like xxx_OVER_FLOW, or arbitrary abbreviations like SNTRJ for "sent
 * reject" etc.)
 */

#define PCILYNX_MAX_REGISTER     0xfff
#define PCILYNX_MAX_MEMORY       0xffff

#define PCI_LATENCY_CACHELINE             0x0c

#define MISC_CONTROL                      0x40
#define MISC_CONTROL_SWRESET              (1<<0)

#define SERIAL_EEPROM_CONTROL             0x44

#define PCI_INT_STATUS                    0x48
#define PCI_INT_ENABLE                    0x4c
/* status and enable have identical bit numbers */
#define PCI_INT_INT_PEND                  (1<<31)
#define PCI_INT_FRC_INT                   (1<<30)
#define PCI_INT_SLV_ADR_PERR              (1<<28)
#define PCI_INT_SLV_DAT_PERR              (1<<27)
#define PCI_INT_MST_DAT_PERR              (1<<26)
#define PCI_INT_MST_DEV_TO                (1<<25)
#define PCI_INT_INT_SLV_TO                (1<<23)
#define PCI_INT_AUX_TO                    (1<<18)
#define PCI_INT_AUX_INT                   (1<<17)
#define PCI_INT_P1394_INT                 (1<<16)
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
#define DMA_WORD0_CMP_VALUE(chan)	(DMA_SREG(DMA0_WORD0_CMP_VALUE, chan))

#define DMA0_WORD0_CMP_ENABLE             0xb04
#define DMA1_WORD0_CMP_ENABLE             0xb14
#define DMA2_WORD0_CMP_ENABLE             0xb24
#define DMA3_WORD0_CMP_ENABLE             0xb34
#define DMA4_WORD0_CMP_ENABLE             0xb44
#define DMA_WORD0_CMP_ENABLE(chan)	(DMA_SREG(DMA0_WORD0_CMP_ENABLE, chan))

#define DMA0_WORD1_CMP_VALUE              0xb08
#define DMA1_WORD1_CMP_VALUE              0xb18
#define DMA2_WORD1_CMP_VALUE              0xb28
#define DMA3_WORD1_CMP_VALUE              0xb38
#define DMA4_WORD1_CMP_VALUE              0xb48
#define DMA_WORD1_CMP_VALUE(chan)	(DMA_SREG(DMA0_WORD1_CMP_VALUE, chan))

#define DMA0_WORD1_CMP_ENABLE             0xb0c
#define DMA1_WORD1_CMP_ENABLE             0xb1c
#define DMA2_WORD1_CMP_ENABLE             0xb2c
#define DMA3_WORD1_CMP_ENABLE             0xb3c
#define DMA4_WORD1_CMP_ENABLE             0xb4c
#define DMA_WORD1_CMP_ENABLE(chan)	(DMA_SREG(DMA0_WORD1_CMP_ENABLE, chan))
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
#define LINK_INT_PHY_TIME_OUT             (1<<30)
#define LINK_INT_PHY_REG_RCVD             (1<<29)
#define LINK_INT_PHY_BUSRESET             (1<<28)
#define LINK_INT_TX_RDY                   (1<<26)
#define LINK_INT_RX_DATA_RDY              (1<<25)
#define LINK_INT_IT_STUCK                 (1<<20)
#define LINK_INT_AT_STUCK                 (1<<19)
#define LINK_INT_SNTRJ                    (1<<17)
#define LINK_INT_HDR_ERR                  (1<<16)
#define LINK_INT_TC_ERR                   (1<<15)
#define LINK_INT_CYC_SEC                  (1<<11)
#define LINK_INT_CYC_STRT                 (1<<10)
#define LINK_INT_CYC_DONE                 (1<<9)
#define LINK_INT_CYC_PEND                 (1<<8)
#define LINK_INT_CYC_LOST                 (1<<7)
#define LINK_INT_CYC_ARB_FAILED           (1<<6)
#define LINK_INT_GRF_OVER_FLOW            (1<<5)
#define LINK_INT_ITF_UNDER_FLOW           (1<<4)
#define LINK_INT_ATF_UNDER_FLOW           (1<<3)
#define LINK_INT_IARB_FAILED              (1<<0)
