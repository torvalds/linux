#ifndef __AM_NET_8218_H_
#define __AM_NET_8218_H_
#include <mach/am_regs.h>
#include <mach/am_eth_reg.h>
#include <linux/io.h>
#include <plat/io.h>
#include <linux/skbuff.h>
#include <linux/phy.h>

/* ETH_MAC_4_GMII_Addr */
#define ETH_MAC_4_GMII_Addr_GB_P                0
#define ETH_MAC_4_GMII_Addr_GW_P                1
#define ETH_MAC_4_GMII_Addr_CR_P                2
#define ETH_MAC_4_GMII_Addr_GR_P                6
#define ETH_MAC_4_GMII_Addr_PA_P                11
  
#define ETH_MAC_4_GMII_Addr_CR_60_100           (0<<ETH_MAC_4_GMII_Addr_CR_P)
#define ETH_MAC_4_GMII_Addr_CR_100_150          (1<<ETH_MAC_4_GMII_Addr_CR_P)
#define ETH_MAC_4_GMII_Addr_CR_20_35            (2<<ETH_MAC_4_GMII_Addr_CR_P)
#define ETH_MAC_4_GMII_Addr_CR_35_60            (3<<ETH_MAC_4_GMII_Addr_CR_P)
#define ETH_MAC_4_GMII_Addr_CR_150_250          (4<<ETH_MAC_4_GMII_Addr_CR_P)
#define ETH_MAC_4_GMII_Addr_CR_250_300          (5<<ETH_MAC_4_GMII_Addr_CR_P)

#define  DMA_USE_SKB_BUF
//#define DMA_USE_MALLOC_ADDR
#define ETH_INTERRUPT	(INT_ETHERNET)
#define ETHBASE (IO_ETH_BASE)
#define WRITE_PERIPHS_REG(v,addr) __raw_writel(v,addr)
#define READ_PERIPHS_REG(addr) __raw_readl(addr)

//#define USE_COHERENT_MEMORY

#ifndef USE_COHERENT_MEMORY
#define CACHE_WSYNC(addr,size)		\
		dma_sync_single_for_device(NULL, (dma_addr_t)virt_to_phys(addr),(size_t)size-1,DMA_TO_DEVICE)
#define CACHE_RSYNC(addr,size)		\
		dma_sync_single_for_cpu(NULL, (dma_addr_t)virt_to_phys(addr),(size_t)size-1,DMA_FROM_DEVICE)
#else
#define CACHE_WSYNC(addr,size)
#define CACHE_RSYNC(addr,size)
#endif

//ring buf must less than the MAX alloc length 131072
//131072/1536~=85;
#define TX_RING_SIZE 	512	    // 512 gives 35-40mb/sec
#define RX_RING_SIZE 	4096  	// 512 gives 51mb/sec
// lower gives slower perf, very high inconsistent with spikes
// 768 gives around 50mb
// RX of 2048 gives around 56mb/sec w/RPS 63mb/sec
// TX of 1024 gives around 31mb/sec w/XPS 40mb/sec
// RX of 4096 gives around 59mb/sec
// TX of 2048 gives around 39mb/sec
// RX of 16384 = 48mb/sec w/RPS = 64mb/sec
// TX of 16384 = 31mb/sec w/XPS = 36mb/sec
#define CACHE_LINE 32
#define IS_CACHE_ALIGNED(x)		(!((unsigned long )x &(CACHE_LINE-1)))
#define CACHE_HEAD_ALIGNED(x)	((x-CACHE_LINE) & (~(CACHE_LINE-1)))
#define CACHE_END_ALIGNED(x)	((x+CACHE_LINE) & (~(CACHE_LINE-1)))
#define PKT_BUF_SZ		1536	/* Size of each temporary Rx buffer. */

#define TX_TIMEOUT 		(HZ * 200 / 1000)
#define GMAC_MMC_Interrupt 1<<27
#define ANOR_INTR_EN 1<<15
#define TX_STOP_EN 1<<1
#define TX_JABBER_TIMEOUT 1<<3
#define RX_FIFO_OVER 1<<4
#define TX_UNDERFLOW 1<<5
#define RX_BUF_UN 1<<7
#define RX_STOP_EN 1<<8
#define RX_WATCH_TIMEOUT 1<<9
#define EARLY_TX_INTR_EN 1<<10
#define FATAL_BUS_ERROR 1<<13

#define NOR_INTR_EN 1<<16
#define TX_INTR_EN  1<<0
#define TX_BUF_UN_EN 1<<2
#define RX_INTR_EN  1<<6
#define EARLY_RX_INTR_EN 1<<14
#define INTERNALPHY_ID 79898963
enum mii_reg_bits {
	MDIO_ShiftClk = 0x10000, MDIO_DataIn = 0x80000, MDIO_DataOut = 0x20000,
	MDIO_EnbOutput = 0x40000, MDIO_EnbIn = 0x00000,
};

enum DmaDescriptorLength {	/* length word of DMA descriptor */
    DescTxIntEnable = 0x80000000,	/* Tx - interrupt on completion                         */
	DescTxLast = 0x40000000,	/* Tx - Last segment of the frame                       */
	DescTxFirst = 0x20000000,	/* Tx - First segment of the frame                      */
	DescTxDisableCrc = 0x04000000,	/* Tx - Add CRC disabled (first segment only)           */
	DescEndOfRing = 0x02000000,	/* End of descriptors ring                              */
	DescChain = 0x01000000,	/* Second buffer address is chain address               */
	DescTxDisablePadd = 0x00800000,	/* disable padding, added by - reyaz */
	DescSize2Mask = 0x003FF800,	/* Buffer 2 size                                        */
	DescSize2Shift = 11, DescSize1Mask = 0x000007FF,	/* Buffer 1 size                                        */
	DescSize1Shift = 0,
};

enum DmaDescriptorStatus {	/* status word of DMA descriptor */
	DescOwnByDma = 0x80000000,	/* Descriptor is owned by DMA engine  */
	// CHANGED: Added on 07/29
	DescDAFilterFail = 0x40000000,	/* Rx - DA Filter Fail for the received frame        E  */
	DescFrameLengthMask = 0x3FFF0000,	/* Receive descriptor frame length */
	DescFrameLengthShift = 16, DescError = 0x00008000,	/* Error summary bit  - OR of the following bits:    v  */
	DescRxTruncated = 0x00004000,	/* Rx - no more descriptors for receive frame        E  */
	// CHANGED: Added on 07/29
	DescSAFilterFail = 0x00002000,	/* Rx - SA Filter Fail for the received frame        E  */
	/* added by reyaz */
	DescRxLengthError = 0x00001000,	/* Rx - frame size not matching with length field    E  */
	DescRxDamaged = 0x00000800,	/* Rx - frame was damaged due to buffer overflow     E  */
	// CHANGED: Added on 07/29
	DescRxVLANTag = 0x00000400,	/* Rx - received frame is a VLAN frame               I  */
	DescRxFirst = 0x00000200,	/* Rx - first descriptor of the frame                I  */
	DescRxLast = 0x00000100,	/* Rx - last descriptor of the frame                 I  */
	DescRxLongFrame = 0x00000080,	/* Rx - frame is longer than 1518 bytes              E  */
	DescRxIPChecksumErr = 0x00000080,	// IPC Checksum Error/Giant Frame
	DescRxCollision = 0x00000040,	/* Rx - late collision occurred during reception     E  */
	DescRxFrameEther = 0x00000020,	/* Rx - Frame type - Ethernet, otherwise 802.3          */
	DescRxWatchdog = 0x00000010,	/* Rx - watchdog timer expired during reception      E  */
	DescRxMiiError = 0x00000008,	/* Rx - error reported by MII interface              E  */
	DescRxDribbling = 0x00000004,	/* Rx - frame contains noninteger multiple of 8 bits    */
	DescRxCrc = 0x00000002,	/* Rx - CRC error                                    E  */
	DescRxTCPChecksumErr = 0x00000001,	//    Payload Checksum Error
	DescTxTimeout = 0x00004000,	/* Tx - Transmit jabber timeout                      E  */
	// CHANGED: Added on 07/29
	DescTxFrameFlushed = 0x00002000,	/* Tx - DMA/MTL flushed the frame due to SW flush    I  */
	DescTxLostCarrier = 0x00000800,	/* Tx - carrier lost during tramsmission             E  */
	DescTxNoCarrier = 0x00000400,	/* Tx - no carrier signal from the tranceiver        E  */
	DescTxLateCollision = 0x00000200,	/* Tx - transmission aborted due to collision        E  */
	DescTxExcCollisions = 0x00000100,	/* Tx - transmission aborted after 16 collisions     E  */
	DescTxVLANFrame = 0x00000080,	/* Tx - VLAN-type frame                                 */
	DescTxCollMask = 0x00000078,	/* Tx - Collision count                                 */
	DescTxCollShift = 3, DescTxExcDeferral = 0x00000004,	/* Tx - excessive deferral                           E  */
	DescTxUnderflow = 0x00000002,	/* Tx - late data arrival from the memory            E  */
	DescTxDeferred = 0x00000001,	/* Tx - frame transmision deferred                      */
};

struct _tx_desc {
	unsigned long status;
	unsigned long count;
	dma_addr_t buf_dma;
	struct _tx_desc *next_dma;

	//-------------------------
	struct sk_buff *skb;
	unsigned long buf;
	struct _tx_desc *next;
	unsigned long reverse[1];
};
struct _rx_desc {
	unsigned long status;
	unsigned long count;
	dma_addr_t buf_dma;
	struct _rx_desc *next_dma;

	//-------------------------
	struct sk_buff *skb;
	unsigned long buf;
	struct _rx_desc *next;
	unsigned long reverse[1];
};

struct am_net_private {
	struct _rx_desc *rx_ring;
	struct _rx_desc *rx_ring_dma;
	struct _tx_desc *tx_ring;
	struct _tx_desc *tx_ring_dma;
	struct _rx_desc *last_rx;
	struct _tx_desc *last_tx;
	struct _tx_desc *start_tx;
	struct net_device *dev;
	struct net_device_stats stats;
	struct timer_list timer;	/* Media monitoring timer. */
	struct tasklet_struct rx_tasklet;
	struct tasklet_struct tx_tasklet;
	int pmt;
	unsigned int irq_mask;

	/* Frequently used values: keep some adjacent for cache effect. */
	spinlock_t lock;
	unsigned int rx_buf_sz;	/* Based on MTU+slack. */
	unsigned int tx_full;	/* The Tx queue is full. */
	int first_tx;

	unsigned long base_addr;

	struct mii_bus *mii;
	phy_interface_t phy_interface;
	int phy_addr;
	int phy_mask; 
	struct phy_device *phydev;
	int oldlink;
	int speed;
	int oldduplex;
	int refcnt;


};

#endif			
