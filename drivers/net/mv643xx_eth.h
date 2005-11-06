#ifndef __MV643XX_ETH_H__
#define __MV643XX_ETH_H__

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include <linux/mv643xx.h>

#define	BIT0	0x00000001
#define	BIT1	0x00000002
#define	BIT2	0x00000004
#define	BIT3	0x00000008
#define	BIT4	0x00000010
#define	BIT5	0x00000020
#define	BIT6	0x00000040
#define	BIT7	0x00000080
#define	BIT8	0x00000100
#define	BIT9	0x00000200
#define	BIT10	0x00000400
#define	BIT11	0x00000800
#define	BIT12	0x00001000
#define	BIT13	0x00002000
#define	BIT14	0x00004000
#define	BIT15	0x00008000
#define	BIT16	0x00010000
#define	BIT17	0x00020000
#define	BIT18	0x00040000
#define	BIT19	0x00080000
#define	BIT20	0x00100000
#define	BIT21	0x00200000
#define	BIT22	0x00400000
#define	BIT23	0x00800000
#define	BIT24	0x01000000
#define	BIT25	0x02000000
#define	BIT26	0x04000000
#define	BIT27	0x08000000
#define	BIT28	0x10000000
#define	BIT29	0x20000000
#define	BIT30	0x40000000
#define	BIT31	0x80000000

/*
 *  The first part is the high level driver of the gigE ethernet ports.
 */

/* Checksum offload for Tx works for most packets, but
 * fails if previous packet sent did not use hw csum
 */
#define	MV643XX_CHECKSUM_OFFLOAD_TX
#define	MV643XX_NAPI
#define	MV643XX_TX_FAST_REFILL
#undef	MV643XX_RX_QUEUE_FILL_ON_TASK	/* Does not work, yet */
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

/*
 * The second part is the low level driver of the gigE ethernet ports.
 */

/*
 * Header File for : MV-643xx network interface header
 *
 * DESCRIPTION:
 *	This header file contains macros typedefs and function declaration for
 *	the Marvell Gig Bit Ethernet Controller.
 *
 * DEPENDENCIES:
 *	None.
 *
 */

/* MAC accepet/reject macros */
#define ACCEPT_MAC_ADDR				0
#define REJECT_MAC_ADDR				1

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
#define ETH_INTERFACE_GMII_MII			0
#define ETH_INTERFACE_PCM			BIT0
#define ETH_LINK_IS_DOWN			0
#define ETH_LINK_IS_UP				BIT1
#define ETH_PORT_AT_HALF_DUPLEX			0
#define ETH_PORT_AT_FULL_DUPLEX			BIT2
#define ETH_RX_FLOW_CTRL_DISABLED		0
#define ETH_RX_FLOW_CTRL_ENBALED		BIT3
#define ETH_GMII_SPEED_100_10			0
#define ETH_GMII_SPEED_1000			BIT4
#define ETH_MII_SPEED_10			0
#define ETH_MII_SPEED_100			BIT5
#define ETH_NO_TX				0
#define ETH_TX_IN_PROGRESS			BIT7
#define ETH_BYPASS_NO_ACTIVE			0
#define ETH_BYPASS_ACTIVE			BIT8
#define ETH_PORT_NOT_AT_PARTITION_STATE		0
#define ETH_PORT_AT_PARTITION_STATE		BIT9
#define ETH_PORT_TX_FIFO_NOT_EMPTY		0
#define ETH_PORT_TX_FIFO_EMPTY			BIT10

#define ETH_DEFAULT_RX_BPDU_QUEUE_3		(BIT23 | BIT22)
#define ETH_DEFAULT_RX_BPDU_QUEUE_4		BIT24
#define ETH_DEFAULT_RX_BPDU_QUEUE_5		(BIT24 | BIT22)
#define ETH_DEFAULT_RX_BPDU_QUEUE_6		(BIT24 | BIT23)
#define ETH_DEFAULT_RX_BPDU_QUEUE_7		(BIT24 | BIT23 | BIT22)

/* SMI reg */
#define ETH_SMI_BUSY		BIT28	/* 0 - Write, 1 - Read		*/
#define ETH_SMI_READ_VALID	BIT27	/* 0 - Write, 1 - Read		*/
#define ETH_SMI_OPCODE_WRITE	0	/* Completion of Read operation */
#define ETH_SMI_OPCODE_READ 	BIT26	/* Operation is in progress	*/

/* SDMA command status fields macros */

/* Tx & Rx descriptors status */
#define ETH_ERROR_SUMMARY			(BIT0)

/* Tx & Rx descriptors command */
#define ETH_BUFFER_OWNED_BY_DMA			(BIT31)

/* Tx descriptors status */
#define ETH_LC_ERROR				(0    )
#define ETH_UR_ERROR				(BIT1 )
#define ETH_RL_ERROR				(BIT2 )
#define ETH_LLC_SNAP_FORMAT			(BIT9 )

/* Rx descriptors status */
#define ETH_CRC_ERROR				(0    )
#define ETH_OVERRUN_ERROR			(BIT1 )
#define ETH_MAX_FRAME_LENGTH_ERROR		(BIT2 )
#define ETH_RESOURCE_ERROR			((BIT2 | BIT1))
#define ETH_VLAN_TAGGED				(BIT19)
#define ETH_BPDU_FRAME				(BIT20)
#define ETH_TCP_FRAME_OVER_IP_V_4		(0    )
#define ETH_UDP_FRAME_OVER_IP_V_4		(BIT21)
#define ETH_OTHER_FRAME_TYPE			(BIT22)
#define ETH_LAYER_2_IS_ETH_V_2			(BIT23)
#define ETH_FRAME_TYPE_IP_V_4			(BIT24)
#define ETH_FRAME_HEADER_OK			(BIT25)
#define ETH_RX_LAST_DESC			(BIT26)
#define ETH_RX_FIRST_DESC			(BIT27)
#define ETH_UNKNOWN_DESTINATION_ADDR		(BIT28)
#define ETH_RX_ENABLE_INTERRUPT			(BIT29)
#define ETH_LAYER_4_CHECKSUM_OK			(BIT30)

/* Rx descriptors byte count */
#define ETH_FRAME_FRAGMENTED			(BIT2)

/* Tx descriptors command */
#define ETH_LAYER_4_CHECKSUM_FIRST_DESC		(BIT10)
#define ETH_FRAME_SET_TO_VLAN			(BIT15)
#define ETH_TCP_FRAME				(0    )
#define ETH_UDP_FRAME				(BIT16)
#define ETH_GEN_TCP_UDP_CHECKSUM		(BIT17)
#define ETH_GEN_IP_V_4_CHECKSUM			(BIT18)
#define ETH_ZERO_PADDING			(BIT19)
#define ETH_TX_LAST_DESC			(BIT20)
#define ETH_TX_FIRST_DESC			(BIT21)
#define ETH_GEN_CRC				(BIT22)
#define ETH_TX_ENABLE_INTERRUPT			(BIT23)
#define ETH_AUTO_MODE				(BIT30)

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

/* Ethernet port specific infomation */

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
	u8 port_mac_addr[6];		/* User defined port MAC address.*/
	u32 port_config;		/* User port configuration value*/
	u32 port_config_extend;		/* User port config extend value*/
	u32 port_sdma_config;		/* User port SDMA config value	*/
	u32 port_serial_control;	/* User port serial control value */
	u32 port_tx_queue_command;	/* Port active Tx queues summary*/
	u32 port_rx_queue_command;	/* Port active Rx queues summary*/

	u32 rx_sram_addr;		/* Base address of rx sram area */
	u32 rx_sram_size;		/* Size of rx sram area		*/
	u32 tx_sram_addr;		/* Base address of tx sram area */
	u32 tx_sram_size;		/* Size of tx sram area		*/

	int rx_resource_err;		/* Rx ring resource error flag */
	int tx_resource_err;		/* Tx ring resource error flag */

	/* Tx/Rx rings managment indexes fields. For driver use */

	/* Next available and first returning Rx resource */
	int rx_curr_desc_q, rx_used_desc_q;

	/* Next available and first returning Tx resource */
	int tx_curr_desc_q, tx_used_desc_q;
#ifdef MV643XX_CHECKSUM_OFFLOAD_TX
	int tx_first_desc_q;
	u32 tx_first_command;
#endif

#ifdef MV643XX_TX_FAST_REFILL
	u32 tx_clean_threshold;
#endif

	struct eth_rx_desc *p_rx_desc_area;
	dma_addr_t rx_desc_dma;
	unsigned int rx_desc_area_size;
	struct sk_buff **rx_skb;

	struct eth_tx_desc *p_tx_desc_area;
	dma_addr_t tx_desc_dma;
	unsigned int tx_desc_area_size;
	struct sk_buff **tx_skb;

	struct work_struct tx_timeout_task;

	/*
	 * Former struct mv643xx_eth_priv members start here
	 */
	struct net_device_stats stats;
	struct mv643xx_mib_counters mib_counters;
	spinlock_t lock;
	/* Size of Tx Ring per queue */
	unsigned int tx_ring_size;
	/* Ammont of SKBs outstanding on Tx queue */
	unsigned int tx_ring_skbs;
	/* Size of Rx Ring per queue */
	unsigned int rx_ring_size;
	/* Ammount of SKBs allocated to Rx Ring per queue */
	unsigned int rx_ring_skbs;

	/*
	 * rx_task used to fill RX ring out of bottom half context
	 */
	struct work_struct rx_task;

	/*
	 * Used in case RX Ring is empty, which can be caused when
	 * system does not have resources (skb's)
	 */
	struct timer_list timeout;
	long rx_task_busy __attribute__ ((aligned(SMP_CACHE_BYTES)));
	unsigned rx_timer_flag;

	u32 rx_int_coal;
	u32 tx_int_coal;
};

/* ethernet.h API list */

/* Port operation control routines */
static void eth_port_init(struct mv643xx_private *mp);
static void eth_port_reset(unsigned int eth_port_num);
static void eth_port_start(struct mv643xx_private *mp);

/* Port MAC address routines */
static void eth_port_uc_addr_set(unsigned int eth_port_num,
				 unsigned char *p_addr);

/* PHY and MIB routines */
static void ethernet_phy_reset(unsigned int eth_port_num);

static void eth_port_write_smi_reg(unsigned int eth_port_num,
				   unsigned int phy_reg, unsigned int value);

static void eth_port_read_smi_reg(unsigned int eth_port_num,
				  unsigned int phy_reg, unsigned int *value);

static void eth_clear_mib_counters(unsigned int eth_port_num);

/* Port data flow control routines */
static ETH_FUNC_RET_STATUS eth_port_send(struct mv643xx_private *mp,
					 struct pkt_info *p_pkt_info);
static ETH_FUNC_RET_STATUS eth_tx_return_desc(struct mv643xx_private *mp,
					      struct pkt_info *p_pkt_info);
static ETH_FUNC_RET_STATUS eth_port_receive(struct mv643xx_private *mp,
					    struct pkt_info *p_pkt_info);
static ETH_FUNC_RET_STATUS eth_rx_return_buff(struct mv643xx_private *mp,
					      struct pkt_info *p_pkt_info);

#endif				/* __MV643XX_ETH_H__ */
