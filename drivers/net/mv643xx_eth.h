#ifndef __MV643XX_ETH_H__
#define __MV643XX_ETH_H__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/mii.h>

#include <linux/mv643xx.h>

#include <asm/dma-mapping.h>

/* Checksum offload for Tx works for most packets, but
 * fails if previous packet sent did not use hw csum
 */
#define	MV643XX_CHECKSUM_OFFLOAD_TX
#define	MV643XX_NAPI
#define	MV643XX_TX_FAST_REFILL
#undef	MV643XX_COAL

/*
 * Number of RX / TX descriptors on RX / TX rings.
 * Note that allocating RX descriptors is done by allocating the RX
 * ring AND a preallocated RX buffers (skb's) for each descriptor.
 * The TX descriptors only allocates the TX descriptors ring,
 * with no pre allocated TX buffers (skb's are allocated by higher layers.
 */

/* Default TX ring size is 1000 descriptors */
#define MV643XX_DEFAULT_TX_QUEUE_SIZE 1000

/* Default RX ring size is 400 descriptors */
#define MV643XX_DEFAULT_RX_QUEUE_SIZE 400

#define MV643XX_TX_COAL 100
#ifdef MV643XX_COAL
#define MV643XX_RX_COAL 100
#endif

#ifdef MV643XX_CHECKSUM_OFFLOAD_TX
#define MAX_DESCS_PER_SKB	(MAX_SKB_FRAGS + 1)
#else
#define MAX_DESCS_PER_SKB	1
#endif

#define ETH_VLAN_HLEN		4
#define ETH_FCS_LEN		4
#define ETH_HW_IP_ALIGN		2		/* hw aligns IP header */
#define ETH_WRAPPER_LEN		(ETH_HW_IP_ALIGN + ETH_HLEN + \
					ETH_VLAN_HLEN + ETH_FCS_LEN)
#define ETH_RX_SKB_SIZE		(dev->mtu + ETH_WRAPPER_LEN + dma_get_cache_alignment())

#define ETH_RX_QUEUES_ENABLED	(1 << 0)	/* use only Q0 for receive */
#define ETH_TX_QUEUES_ENABLED	(1 << 0)	/* use only Q0 for transmit */

#define ETH_INT_CAUSE_RX_DONE	(ETH_RX_QUEUES_ENABLED << 2)
#define ETH_INT_CAUSE_RX_ERROR	(ETH_RX_QUEUES_ENABLED << 9)
#define ETH_INT_CAUSE_RX	(ETH_INT_CAUSE_RX_DONE | ETH_INT_CAUSE_RX_ERROR)
#define ETH_INT_CAUSE_EXT	0x00000002
#define ETH_INT_UNMASK_ALL	(ETH_INT_CAUSE_RX | ETH_INT_CAUSE_EXT)

#define ETH_INT_CAUSE_TX_DONE	(ETH_TX_QUEUES_ENABLED << 0)
#define ETH_INT_CAUSE_TX_ERROR	(ETH_TX_QUEUES_ENABLED << 8)
#define ETH_INT_CAUSE_TX	(ETH_INT_CAUSE_TX_DONE | ETH_INT_CAUSE_TX_ERROR)
#define ETH_INT_CAUSE_PHY	0x00010000
#define ETH_INT_CAUSE_STATE	0x00100000
#define ETH_INT_UNMASK_ALL_EXT	(ETH_INT_CAUSE_TX | ETH_INT_CAUSE_PHY | \
					ETH_INT_CAUSE_STATE)

#define ETH_INT_MASK_ALL	0x00000000
#define ETH_INT_MASK_ALL_EXT	0x00000000

#define PHY_WAIT_ITERATIONS	1000	/* 1000 iterations * 10uS = 10mS max */
#define PHY_WAIT_MICRO_SECONDS	10

/* Buffer offset from buffer pointer */
#define RX_BUF_OFFSET				0x2

/* Gigabit Ethernet Unit Global Registers */

/* MIB Counters register definitions */
#define ETH_MIB_GOOD_OCTETS_RECEIVED_LOW	0x0
#define ETH_MIB_GOOD_OCTETS_RECEIVED_HIGH	0x4
#define ETH_MIB_BAD_OCTETS_RECEIVED		0x8
#define ETH_MIB_INTERNAL_MAC_TRANSMIT_ERR	0xc
#define ETH_MIB_GOOD_FRAMES_RECEIVED		0x10
#define ETH_MIB_BAD_FRAMES_RECEIVED		0x14
#define ETH_MIB_BROADCAST_FRAMES_RECEIVED	0x18
#define ETH_MIB_MULTICAST_FRAMES_RECEIVED	0x1c
#define ETH_MIB_FRAMES_64_OCTETS		0x20
#define ETH_MIB_FRAMES_65_TO_127_OCTETS		0x24
#define ETH_MIB_FRAMES_128_TO_255_OCTETS	0x28
#define ETH_MIB_FRAMES_256_TO_511_OCTETS	0x2c
#define ETH_MIB_FRAMES_512_TO_1023_OCTETS	0x30
#define ETH_MIB_FRAMES_1024_TO_MAX_OCTETS	0x34
#define ETH_MIB_GOOD_OCTETS_SENT_LOW		0x38
#define ETH_MIB_GOOD_OCTETS_SENT_HIGH		0x3c
#define ETH_MIB_GOOD_FRAMES_SENT		0x40
#define ETH_MIB_EXCESSIVE_COLLISION		0x44
#define ETH_MIB_MULTICAST_FRAMES_SENT		0x48
#define ETH_MIB_BROADCAST_FRAMES_SENT		0x4c
#define ETH_MIB_UNREC_MAC_CONTROL_RECEIVED	0x50
#define ETH_MIB_FC_SENT				0x54
#define ETH_MIB_GOOD_FC_RECEIVED		0x58
#define ETH_MIB_BAD_FC_RECEIVED			0x5c
#define ETH_MIB_UNDERSIZE_RECEIVED		0x60
#define ETH_MIB_FRAGMENTS_RECEIVED		0x64
#define ETH_MIB_OVERSIZE_RECEIVED		0x68
#define ETH_MIB_JABBER_RECEIVED			0x6c
#define ETH_MIB_MAC_RECEIVE_ERROR		0x70
#define ETH_MIB_BAD_CRC_EVENT			0x74
#define ETH_MIB_COLLISION			0x78
#define ETH_MIB_LATE_COLLISION			0x7c

/* Port serial status reg (PSR) */
#define ETH_INTERFACE_PCM			0x00000001
#define ETH_LINK_IS_UP				0x00000002
#define ETH_PORT_AT_FULL_DUPLEX			0x00000004
#define ETH_RX_FLOW_CTRL_ENABLED		0x00000008
#define ETH_GMII_SPEED_1000			0x00000010
#define ETH_MII_SPEED_100			0x00000020
#define ETH_TX_IN_PROGRESS			0x00000080
#define ETH_BYPASS_ACTIVE			0x00000100
#define ETH_PORT_AT_PARTITION_STATE		0x00000200
#define ETH_PORT_TX_FIFO_EMPTY			0x00000400

/* SMI reg */
#define ETH_SMI_BUSY		0x10000000	/* 0 - Write, 1 - Read	*/
#define ETH_SMI_READ_VALID	0x08000000	/* 0 - Write, 1 - Read	*/
#define ETH_SMI_OPCODE_WRITE	0		/* Completion of Read	*/
#define ETH_SMI_OPCODE_READ 	0x04000000	/* Operation is in progress */

/* Interrupt Cause Register Bit Definitions */

/* SDMA command status fields macros */

/* Tx & Rx descriptors status */
#define ETH_ERROR_SUMMARY			0x00000001

/* Tx & Rx descriptors command */
#define ETH_BUFFER_OWNED_BY_DMA			0x80000000

/* Tx descriptors status */
#define ETH_LC_ERROR				0
#define ETH_UR_ERROR				0x00000002
#define ETH_RL_ERROR				0x00000004
#define ETH_LLC_SNAP_FORMAT			0x00000200

/* Rx descriptors status */
#define ETH_OVERRUN_ERROR			0x00000002
#define ETH_MAX_FRAME_LENGTH_ERROR		0x00000004
#define ETH_RESOURCE_ERROR			0x00000006
#define ETH_VLAN_TAGGED				0x00080000
#define ETH_BPDU_FRAME				0x00100000
#define ETH_UDP_FRAME_OVER_IP_V_4		0x00200000
#define ETH_OTHER_FRAME_TYPE			0x00400000
#define ETH_LAYER_2_IS_ETH_V_2			0x00800000
#define ETH_FRAME_TYPE_IP_V_4			0x01000000
#define ETH_FRAME_HEADER_OK			0x02000000
#define ETH_RX_LAST_DESC			0x04000000
#define ETH_RX_FIRST_DESC			0x08000000
#define ETH_UNKNOWN_DESTINATION_ADDR		0x10000000
#define ETH_RX_ENABLE_INTERRUPT			0x20000000
#define ETH_LAYER_4_CHECKSUM_OK			0x40000000

/* Rx descriptors byte count */
#define ETH_FRAME_FRAGMENTED			0x00000004

/* Tx descriptors command */
#define ETH_LAYER_4_CHECKSUM_FIRST_DESC		0x00000400
#define ETH_FRAME_SET_TO_VLAN			0x00008000
#define ETH_UDP_FRAME				0x00010000
#define ETH_GEN_TCP_UDP_CHECKSUM		0x00020000
#define ETH_GEN_IP_V_4_CHECKSUM			0x00040000
#define ETH_ZERO_PADDING			0x00080000
#define ETH_TX_LAST_DESC			0x00100000
#define ETH_TX_FIRST_DESC			0x00200000
#define ETH_GEN_CRC				0x00400000
#define ETH_TX_ENABLE_INTERRUPT			0x00800000
#define ETH_AUTO_MODE				0x40000000

#define ETH_TX_IHL_SHIFT			11

/* typedefs */

typedef enum _eth_func_ret_status {
	ETH_OK,			/* Returned as expected.		*/
	ETH_ERROR,		/* Fundamental error.			*/
	ETH_RETRY,		/* Could not process request. Try later.*/
	ETH_END_OF_JOB,		/* Ring has nothing to process.		*/
	ETH_QUEUE_FULL,		/* Ring resource error.			*/
	ETH_QUEUE_LAST_RESOURCE	/* Ring resources about to exhaust.	*/
} ETH_FUNC_RET_STATUS;

typedef enum _eth_target {
	ETH_TARGET_DRAM,
	ETH_TARGET_DEVICE,
	ETH_TARGET_CBS,
	ETH_TARGET_PCI0,
	ETH_TARGET_PCI1
} ETH_TARGET;

/* These are for big-endian machines.  Little endian needs different
 * definitions.
 */
#if defined(__BIG_ENDIAN)
struct eth_rx_desc {
	u16 byte_cnt;		/* Descriptor buffer byte count		*/
	u16 buf_size;		/* Buffer size				*/
	u32 cmd_sts;		/* Descriptor command status		*/
	u32 next_desc_ptr;	/* Next descriptor pointer		*/
	u32 buf_ptr;		/* Descriptor buffer pointer		*/
};

struct eth_tx_desc {
	u16 byte_cnt;		/* buffer byte count			*/
	u16 l4i_chk;		/* CPU provided TCP checksum		*/
	u32 cmd_sts;		/* Command/status field			*/
	u32 next_desc_ptr;	/* Pointer to next descriptor		*/
	u32 buf_ptr;		/* pointer to buffer for this descriptor*/
};

#elif defined(__LITTLE_ENDIAN)
struct eth_rx_desc {
	u32 cmd_sts;		/* Descriptor command status		*/
	u16 buf_size;		/* Buffer size				*/
	u16 byte_cnt;		/* Descriptor buffer byte count		*/
	u32 buf_ptr;		/* Descriptor buffer pointer		*/
	u32 next_desc_ptr;	/* Next descriptor pointer		*/
};

struct eth_tx_desc {
	u32 cmd_sts;		/* Command/status field			*/
	u16 l4i_chk;		/* CPU provided TCP checksum		*/
	u16 byte_cnt;		/* buffer byte count			*/
	u32 buf_ptr;		/* pointer to buffer for this descriptor*/
	u32 next_desc_ptr;	/* Pointer to next descriptor		*/
};
#else
#error One of __BIG_ENDIAN or __LITTLE_ENDIAN must be defined
#endif

/* Unified struct for Rx and Tx operations. The user is not required to	*/
/* be familier with neither Tx nor Rx descriptors.			*/
struct pkt_info {
	unsigned short byte_cnt;	/* Descriptor buffer byte count	*/
	unsigned short l4i_chk;		/* Tx CPU provided TCP Checksum	*/
	unsigned int cmd_sts;		/* Descriptor command status	*/
	dma_addr_t buf_ptr;		/* Descriptor buffer pointer	*/
	struct sk_buff *return_info;	/* User resource return information */
};

/* Ethernet port specific information */

struct mv643xx_mib_counters {
	u64 good_octets_received;
	u32 bad_octets_received;
	u32 internal_mac_transmit_err;
	u32 good_frames_received;
	u32 bad_frames_received;
	u32 broadcast_frames_received;
	u32 multicast_frames_received;
	u32 frames_64_octets;
	u32 frames_65_to_127_octets;
	u32 frames_128_to_255_octets;
	u32 frames_256_to_511_octets;
	u32 frames_512_to_1023_octets;
	u32 frames_1024_to_max_octets;
	u64 good_octets_sent;
	u32 good_frames_sent;
	u32 excessive_collision;
	u32 multicast_frames_sent;
	u32 broadcast_frames_sent;
	u32 unrec_mac_control_received;
	u32 fc_sent;
	u32 good_fc_received;
	u32 bad_fc_received;
	u32 undersize_received;
	u32 fragments_received;
	u32 oversize_received;
	u32 jabber_received;
	u32 mac_receive_error;
	u32 bad_crc_event;
	u32 collision;
	u32 late_collision;
};

struct mv643xx_private {
	int port_num;			/* User Ethernet port number	*/

	u32 rx_sram_addr;		/* Base address of rx sram area */
	u32 rx_sram_size;		/* Size of rx sram area		*/
	u32 tx_sram_addr;		/* Base address of tx sram area */
	u32 tx_sram_size;		/* Size of tx sram area		*/

	int rx_resource_err;		/* Rx ring resource error flag */

	/* Tx/Rx rings managment indexes fields. For driver use */

	/* Next available and first returning Rx resource */
	int rx_curr_desc_q, rx_used_desc_q;

	/* Next available and first returning Tx resource */
	int tx_curr_desc_q, tx_used_desc_q;

#ifdef MV643XX_TX_FAST_REFILL
	u32 tx_clean_threshold;
#endif

	struct eth_rx_desc *p_rx_desc_area;
	dma_addr_t rx_desc_dma;
	int rx_desc_area_size;
	struct sk_buff **rx_skb;

	struct eth_tx_desc *p_tx_desc_area;
	dma_addr_t tx_desc_dma;
	int tx_desc_area_size;
	struct sk_buff **tx_skb;

	struct work_struct tx_timeout_task;

	struct net_device *dev;
	struct napi_struct napi;
	struct net_device_stats stats;
	struct mv643xx_mib_counters mib_counters;
	spinlock_t lock;
	/* Size of Tx Ring per queue */
	int tx_ring_size;
	/* Number of tx descriptors in use */
	int tx_desc_count;
	/* Size of Rx Ring per queue */
	int rx_ring_size;
	/* Number of rx descriptors in use */
	int rx_desc_count;

	/*
	 * Used in case RX Ring is empty, which can be caused when
	 * system does not have resources (skb's)
	 */
	struct timer_list timeout;

	u32 rx_int_coal;
	u32 tx_int_coal;
	struct mii_if_info mii;
};

/* Port operation control routines */
static void eth_port_init(struct mv643xx_private *mp);
static void eth_port_reset(unsigned int eth_port_num);
static void eth_port_start(struct net_device *dev);

/* PHY and MIB routines */
static void ethernet_phy_reset(unsigned int eth_port_num);

static void eth_port_write_smi_reg(unsigned int eth_port_num,
				   unsigned int phy_reg, unsigned int value);

static void eth_port_read_smi_reg(unsigned int eth_port_num,
				  unsigned int phy_reg, unsigned int *value);

static void eth_clear_mib_counters(unsigned int eth_port_num);

/* Port data flow control routines */
static ETH_FUNC_RET_STATUS eth_port_receive(struct mv643xx_private *mp,
					    struct pkt_info *p_pkt_info);
static ETH_FUNC_RET_STATUS eth_rx_return_buff(struct mv643xx_private *mp,
					      struct pkt_info *p_pkt_info);

#endif				/* __MV643XX_ETH_H__ */
