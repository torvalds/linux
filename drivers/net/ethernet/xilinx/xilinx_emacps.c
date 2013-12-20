/*
 * Xilinx Ethernet: Linux driver for Ethernet.
 *
 * Author: Xilinx, Inc.
 *
 * 2010 (c) Xilinx, Inc. This file is licensed uner the terms of the GNU
 * General Public License version 2. This program is licensed "as is"
 * without any warranty of any kind, whether express or implied.
 *
 * This is a driver for xilinx processor sub-system (ps) ethernet device.
 * This driver is mainly used in Linux 2.6.30 and above and it does _not_
 * support Linux 2.4 kernel due to certain new features (e.g. NAPI) is
 * introduced in this driver.
 *
 * TODO:
 * 1. JUMBO frame is not enabled per EPs spec. Please update it if this
 *    support is added in and set MAX_MTU to 9000.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <linux/mii.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/ethtool.h>
#include <linux/vmalloc.h>
#include <linux/version.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/clocksource.h>
#include <linux/net_tstamp.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/of_net.h>
#include <linux/of_address.h>
#include <linux/of_mdio.h>
#include <linux/timer.h>

/************************** Constant Definitions *****************************/

/* Must be shorter than length of ethtool_drvinfo.driver field to fit */
#define DRIVER_NAME			"xemacps"
#define DRIVER_DESCRIPTION		"Xilinx Tri-Mode Ethernet MAC driver"
#define DRIVER_VERSION			"1.00a"

/* Transmission timeout is 3 seconds. */
#define TX_TIMEOUT			(3*HZ)

/* for RX skb IP header word-aligned */
#define RX_IP_ALIGN_OFFSET		2

/* DMA buffer descriptors must be aligned on a 4-byte boundary. */
#define ALIGNMENT_BD			8

/* Maximum value for hash bits. 2**6 */
#define XEMACPS_MAX_HASH_BITS		64

/* MDC clock division
 * currently supporting 8, 16, 32, 48, 64, 96, 128, 224.
 */
enum { MDC_DIV_8 = 0, MDC_DIV_16, MDC_DIV_32, MDC_DIV_48,
MDC_DIV_64, MDC_DIV_96, MDC_DIV_128, MDC_DIV_224 };

/* Specify the receive buffer size in bytes, 64, 128, 192, 10240 */
#define XEMACPS_RX_BUF_SIZE		1536

/* Number of receive buffer bytes as a unit, this is HW setup */
#define XEMACPS_RX_BUF_UNIT		64

/* Default SEND and RECV buffer descriptors (BD) numbers.
 * BD Space needed is (XEMACPS_SEND_BD_CNT+XEMACPS_RECV_BD_CNT)*8
 */
#undef  DEBUG
#define DEBUG

#define XEMACPS_SEND_BD_CNT		256
#define XEMACPS_RECV_BD_CNT		256

#define XEMACPS_NAPI_WEIGHT		64

/* Register offset definitions. Unless otherwise noted, register access is
 * 32 bit. Names are self explained here.
 */
#define XEMACPS_NWCTRL_OFFSET		0x00000000 /* Network Control reg */
#define XEMACPS_NWCFG_OFFSET		0x00000004 /* Network Config reg */
#define XEMACPS_NWSR_OFFSET		0x00000008 /* Network Status reg */
#define XEMACPS_USERIO_OFFSET		0x0000000C /* User IO reg */
#define XEMACPS_DMACR_OFFSET		0x00000010 /* DMA Control reg */
#define XEMACPS_TXSR_OFFSET		0x00000014 /* TX Status reg */
#define XEMACPS_RXQBASE_OFFSET		0x00000018 /* RX Q Base address reg */
#define XEMACPS_TXQBASE_OFFSET		0x0000001C /* TX Q Base address reg */
#define XEMACPS_RXSR_OFFSET		0x00000020 /* RX Status reg */
#define XEMACPS_ISR_OFFSET		0x00000024 /* Interrupt Status reg */
#define XEMACPS_IER_OFFSET		0x00000028 /* Interrupt Enable reg */
#define XEMACPS_IDR_OFFSET		0x0000002C /* Interrupt Disable reg */
#define XEMACPS_IMR_OFFSET		0x00000030 /* Interrupt Mask reg */
#define XEMACPS_PHYMNTNC_OFFSET	0x00000034 /* Phy Maintaince reg */
#define XEMACPS_RXPAUSE_OFFSET		0x00000038 /* RX Pause Time reg */
#define XEMACPS_TXPAUSE_OFFSET		0x0000003C /* TX Pause Time reg */
#define XEMACPS_HASHL_OFFSET		0x00000080 /* Hash Low address reg */
#define XEMACPS_HASHH_OFFSET		0x00000084 /* Hash High address reg */
#define XEMACPS_LADDR1L_OFFSET		0x00000088 /* Specific1 addr low */
#define XEMACPS_LADDR1H_OFFSET		0x0000008C /* Specific1 addr high */
#define XEMACPS_LADDR2L_OFFSET		0x00000090 /* Specific2 addr low */
#define XEMACPS_LADDR2H_OFFSET		0x00000094 /* Specific2 addr high */
#define XEMACPS_LADDR3L_OFFSET		0x00000098 /* Specific3 addr low */
#define XEMACPS_LADDR3H_OFFSET		0x0000009C /* Specific3 addr high */
#define XEMACPS_LADDR4L_OFFSET		0x000000A0 /* Specific4 addr low */
#define XEMACPS_LADDR4H_OFFSET		0x000000A4 /* Specific4 addr high */
#define XEMACPS_MATCH1_OFFSET		0x000000A8 /* Type ID1 Match reg */
#define XEMACPS_MATCH2_OFFSET		0x000000AC /* Type ID2 Match reg */
#define XEMACPS_MATCH3_OFFSET		0x000000B0 /* Type ID3 Match reg */
#define XEMACPS_MATCH4_OFFSET		0x000000B4 /* Type ID4 Match reg */
#define XEMACPS_WOL_OFFSET		0x000000B8 /* Wake on LAN reg */
#define XEMACPS_STRETCH_OFFSET		0x000000BC /* IPG Stretch reg */
#define XEMACPS_SVLAN_OFFSET		0x000000C0 /* Stacked VLAN reg */
#define XEMACPS_MODID_OFFSET		0x000000FC /* Module ID reg */
#define XEMACPS_OCTTXL_OFFSET		0x00000100 /* Octects transmitted Low
						reg */
#define XEMACPS_OCTTXH_OFFSET		0x00000104 /* Octects transmitted High
						reg */
#define XEMACPS_TXCNT_OFFSET		0x00000108 /* Error-free Frmaes
						transmitted counter */
#define XEMACPS_TXBCCNT_OFFSET		0x0000010C /* Error-free Broadcast
						Frames counter*/
#define XEMACPS_TXMCCNT_OFFSET		0x00000110 /* Error-free Multicast
						Frame counter */
#define XEMACPS_TXPAUSECNT_OFFSET	0x00000114 /* Pause Frames Transmitted
						Counter */
#define XEMACPS_TX64CNT_OFFSET		0x00000118 /* Error-free 64 byte Frames
						Transmitted counter */
#define XEMACPS_TX65CNT_OFFSET		0x0000011C /* Error-free 65-127 byte
						Frames Transmitted counter */
#define XEMACPS_TX128CNT_OFFSET	0x00000120 /* Error-free 128-255 byte
						Frames Transmitted counter */
#define XEMACPS_TX256CNT_OFFSET	0x00000124 /* Error-free 256-511 byte
						Frames transmitted counter */
#define XEMACPS_TX512CNT_OFFSET	0x00000128 /* Error-free 512-1023 byte
						Frames transmitted counter */
#define XEMACPS_TX1024CNT_OFFSET	0x0000012C /* Error-free 1024-1518 byte
						Frames transmitted counter */
#define XEMACPS_TX1519CNT_OFFSET	0x00000130 /* Error-free larger than
						1519 byte Frames transmitted
						Counter */
#define XEMACPS_TXURUNCNT_OFFSET	0x00000134 /* TX under run error
						Counter */
#define XEMACPS_SNGLCOLLCNT_OFFSET	0x00000138 /* Single Collision Frame
						Counter */
#define XEMACPS_MULTICOLLCNT_OFFSET	0x0000013C /* Multiple Collision Frame
						Counter */
#define XEMACPS_EXCESSCOLLCNT_OFFSET	0x00000140 /* Excessive Collision Frame
						Counter */
#define XEMACPS_LATECOLLCNT_OFFSET	0x00000144 /* Late Collision Frame
						Counter */
#define XEMACPS_TXDEFERCNT_OFFSET	0x00000148 /* Deferred Transmission
						Frame Counter */
#define XEMACPS_CSENSECNT_OFFSET	0x0000014C /* Carrier Sense Error
						Counter */
#define XEMACPS_OCTRXL_OFFSET		0x00000150 /* Octects Received register
						Low */
#define XEMACPS_OCTRXH_OFFSET		0x00000154 /* Octects Received register
						High */
#define XEMACPS_RXCNT_OFFSET		0x00000158 /* Error-free Frames
						Received Counter */
#define XEMACPS_RXBROADCNT_OFFSET	0x0000015C /* Error-free Broadcast
						Frames Received Counter */
#define XEMACPS_RXMULTICNT_OFFSET	0x00000160 /* Error-free Multicast
						Frames Received Counter */
#define XEMACPS_RXPAUSECNT_OFFSET	0x00000164 /* Pause Frames
						Received Counter */
#define XEMACPS_RX64CNT_OFFSET		0x00000168 /* Error-free 64 byte Frames
						Received Counter */
#define XEMACPS_RX65CNT_OFFSET		0x0000016C /* Error-free 65-127 byte
						Frames Received Counter */
#define XEMACPS_RX128CNT_OFFSET	0x00000170 /* Error-free 128-255 byte
						Frames Received Counter */
#define XEMACPS_RX256CNT_OFFSET	0x00000174 /* Error-free 256-512 byte
						Frames Received Counter */
#define XEMACPS_RX512CNT_OFFSET	0x00000178 /* Error-free 512-1023 byte
						Frames Received Counter */
#define XEMACPS_RX1024CNT_OFFSET	0x0000017C /* Error-free 1024-1518 byte
						Frames Received Counter */
#define XEMACPS_RX1519CNT_OFFSET	0x00000180 /* Error-free 1519-max byte
						Frames Received Counter */
#define XEMACPS_RXUNDRCNT_OFFSET	0x00000184 /* Undersize Frames Received
						Counter */
#define XEMACPS_RXOVRCNT_OFFSET	0x00000188 /* Oversize Frames Received
						Counter */
#define XEMACPS_RXJABCNT_OFFSET	0x0000018C /* Jabbers Received
						Counter */
#define XEMACPS_RXFCSCNT_OFFSET	0x00000190 /* Frame Check Sequence
						Error Counter */
#define XEMACPS_RXLENGTHCNT_OFFSET	0x00000194 /* Length Field Error
						Counter */
#define XEMACPS_RXSYMBCNT_OFFSET	0x00000198 /* Symbol Error Counter */
#define XEMACPS_RXALIGNCNT_OFFSET	0x0000019C /* Alignment Error
						Counter */
#define XEMACPS_RXRESERRCNT_OFFSET	0x000001A0 /* Receive Resource Error
						Counter */
#define XEMACPS_RXORCNT_OFFSET		0x000001A4 /* Receive Overrun */
#define XEMACPS_RXIPCCNT_OFFSET	0x000001A8 /* IP header Checksum Error
						Counter */
#define XEMACPS_RXTCPCCNT_OFFSET	0x000001AC /* TCP Checksum Error
						Counter */
#define XEMACPS_RXUDPCCNT_OFFSET	0x000001B0 /* UDP Checksum Error
						Counter */

#define XEMACPS_1588S_OFFSET		0x000001D0 /* 1588 Timer Seconds */
#define XEMACPS_1588NS_OFFSET		0x000001D4 /* 1588 Timer Nanoseconds */
#define XEMACPS_1588ADJ_OFFSET		0x000001D8 /* 1588 Timer Adjust */
#define XEMACPS_1588INC_OFFSET		0x000001DC /* 1588 Timer Increment */
#define XEMACPS_PTPETXS_OFFSET		0x000001E0 /* PTP Event Frame
						Transmitted Seconds */
#define XEMACPS_PTPETXNS_OFFSET	0x000001E4 /* PTP Event Frame
						Transmitted Nanoseconds */
#define XEMACPS_PTPERXS_OFFSET		0x000001E8 /* PTP Event Frame Received
						Seconds */
#define XEMACPS_PTPERXNS_OFFSET	0x000001EC /* PTP Event Frame Received
						Nanoseconds */
#define XEMACPS_PTPPTXS_OFFSET		0x000001E0 /* PTP Peer Frame
						Transmitted Seconds */
#define XEMACPS_PTPPTXNS_OFFSET	0x000001E4 /* PTP Peer Frame
						Transmitted Nanoseconds */
#define XEMACPS_PTPPRXS_OFFSET		0x000001E8 /* PTP Peer Frame Received
						Seconds */
#define XEMACPS_PTPPRXNS_OFFSET	0x000001EC /* PTP Peer Frame Received
						Nanoseconds */

/* network control register bit definitions */
#define XEMACPS_NWCTRL_FLUSH_DPRAM_MASK	0x00040000
#define XEMACPS_NWCTRL_RXTSTAMP_MASK	0x00008000 /* RX Timestamp in CRC */
#define XEMACPS_NWCTRL_ZEROPAUSETX_MASK 0x00001000 /* Transmit zero quantum
						pause frame */
#define XEMACPS_NWCTRL_PAUSETX_MASK	0x00000800 /* Transmit pause frame */
#define XEMACPS_NWCTRL_HALTTX_MASK	0x00000400 /* Halt transmission
						after current frame */
#define XEMACPS_NWCTRL_STARTTX_MASK	0x00000200 /* Start tx (tx_go) */

#define XEMACPS_NWCTRL_STATWEN_MASK	0x00000080 /* Enable writing to
						stat counters */
#define XEMACPS_NWCTRL_STATINC_MASK	0x00000040 /* Increment statistic
						registers */
#define XEMACPS_NWCTRL_STATCLR_MASK	0x00000020 /* Clear statistic
						registers */
#define XEMACPS_NWCTRL_MDEN_MASK	0x00000010 /* Enable MDIO port */
#define XEMACPS_NWCTRL_TXEN_MASK	0x00000008 /* Enable transmit */
#define XEMACPS_NWCTRL_RXEN_MASK	0x00000004 /* Enable receive */
#define XEMACPS_NWCTRL_LOOPEN_MASK	0x00000002 /* local loopback */

/* name network configuration register bit definitions */
#define XEMACPS_NWCFG_BADPREAMBEN_MASK	0x20000000 /* disable rejection of
						non-standard preamble */
#define XEMACPS_NWCFG_IPDSTRETCH_MASK	0x10000000 /* enable transmit IPG */
#define XEMACPS_NWCFG_FCSIGNORE_MASK	0x04000000 /* disable rejection of
						FCS error */
#define XEMACPS_NWCFG_HDRXEN_MASK	0x02000000 /* RX half duplex */
#define XEMACPS_NWCFG_RXCHKSUMEN_MASK	0x01000000 /* enable RX checksum
						offload */
#define XEMACPS_NWCFG_PAUSECOPYDI_MASK	0x00800000 /* Do not copy pause
						Frames to memory */
#define XEMACPS_NWCFG_MDC_SHIFT_MASK	18 /* shift bits for MDC */
#define XEMACPS_NWCFG_MDCCLKDIV_MASK	0x001C0000 /* MDC Mask PCLK divisor */
#define XEMACPS_NWCFG_FCSREM_MASK	0x00020000 /* Discard FCS from
						received frames */
#define XEMACPS_NWCFG_LENGTHERRDSCRD_MASK 0x00010000
/* RX length error discard */
#define XEMACPS_NWCFG_RXOFFS_MASK	0x0000C000 /* RX buffer offset */
#define XEMACPS_NWCFG_PAUSEEN_MASK	0x00002000 /* Enable pause TX */
#define XEMACPS_NWCFG_RETRYTESTEN_MASK	0x00001000 /* Retry test */
#define XEMACPS_NWCFG_1000_MASK		0x00000400 /* Gigbit mode */
#define XEMACPS_NWCFG_EXTADDRMATCHEN_MASK	0x00000200
/* External address match enable */
#define XEMACPS_NWCFG_UCASTHASHEN_MASK	0x00000080 /* Receive unicast hash
						frames */
#define XEMACPS_NWCFG_MCASTHASHEN_MASK	0x00000040 /* Receive multicast hash
						frames */
#define XEMACPS_NWCFG_BCASTDI_MASK	0x00000020 /* Do not receive
						broadcast frames */
#define XEMACPS_NWCFG_COPYALLEN_MASK	0x00000010 /* Copy all frames */

#define XEMACPS_NWCFG_NVLANDISC_MASK	0x00000004 /* Receive only VLAN
						frames */
#define XEMACPS_NWCFG_FDEN_MASK		0x00000002 /* Full duplex */
#define XEMACPS_NWCFG_100_MASK		0x00000001 /* 10 or 100 Mbs */

/* network status register bit definitaions */
#define XEMACPS_NWSR_MDIOIDLE_MASK	0x00000004 /* PHY management idle */
#define XEMACPS_NWSR_MDIO_MASK		0x00000002 /* Status of mdio_in */

/* MAC address register word 1 mask */
#define XEMACPS_LADDR_MACH_MASK	0x0000FFFF /* Address bits[47:32]
						bit[31:0] are in BOTTOM */

/* DMA control register bit definitions */
#define XEMACPS_DMACR_RXBUF_MASK	0x00FF0000 /* Mask bit for RX buffer
						size */
#define XEMACPS_DMACR_RXBUF_SHIFT	16 /* Shift bit for RX buffer
						size */
#define XEMACPS_DMACR_TCPCKSUM_MASK	0x00000800 /* enable/disable TX
						checksum offload */
#define XEMACPS_DMACR_TXSIZE_MASK	0x00000400 /* TX buffer memory size */
#define XEMACPS_DMACR_RXSIZE_MASK	0x00000300 /* RX buffer memory size */
#define XEMACPS_DMACR_ENDIAN_MASK	0x00000080 /* Endian configuration */
#define XEMACPS_DMACR_BLENGTH_MASK	0x0000001F /* Buffer burst length */
#define XEMACPS_DMACR_BLENGTH_INCR16	0x00000010 /* Buffer burst length */
#define XEMACPS_DMACR_BLENGTH_INCR8	0x00000008 /* Buffer burst length */
#define XEMACPS_DMACR_BLENGTH_INCR4	0x00000004 /* Buffer burst length */
#define XEMACPS_DMACR_BLENGTH_SINGLE	0x00000002 /* Buffer burst length */

/* transmit status register bit definitions */
#define XEMACPS_TXSR_HRESPNOK_MASK	0x00000100 /* Transmit hresp not OK */
#define XEMACPS_TXSR_COL1000_MASK	0x00000080 /* Collision Gbs mode */
#define XEMACPS_TXSR_URUN_MASK		0x00000040 /* Transmit underrun */
#define XEMACPS_TXSR_TXCOMPL_MASK	0x00000020 /* Transmit completed OK */
#define XEMACPS_TXSR_BUFEXH_MASK	0x00000010 /* Transmit buffs exhausted
						mid frame */
#define XEMACPS_TXSR_TXGO_MASK		0x00000008 /* Status of go flag */
#define XEMACPS_TXSR_RXOVR_MASK	0x00000004 /* Retry limit exceeded */
#define XEMACPS_TXSR_COL100_MASK	0x00000002 /* Collision 10/100  mode */
#define XEMACPS_TXSR_USEDREAD_MASK	0x00000001 /* TX buffer used bit set */

#define XEMACPS_TXSR_ERROR_MASK	(XEMACPS_TXSR_HRESPNOK_MASK |	\
					XEMACPS_TXSR_COL1000_MASK |	\
					XEMACPS_TXSR_URUN_MASK |	\
					XEMACPS_TXSR_BUFEXH_MASK |	\
					XEMACPS_TXSR_RXOVR_MASK |	\
					XEMACPS_TXSR_COL100_MASK |	\
					XEMACPS_TXSR_USEDREAD_MASK)

/* receive status register bit definitions */
#define XEMACPS_RXSR_HRESPNOK_MASK	0x00000008 /* Receive hresp not OK */
#define XEMACPS_RXSR_RXOVR_MASK	0x00000004 /* Receive overrun */
#define XEMACPS_RXSR_FRAMERX_MASK	0x00000002 /* Frame received OK */
#define XEMACPS_RXSR_BUFFNA_MASK	0x00000001 /* RX buffer used bit set */

#define XEMACPS_RXSR_ERROR_MASK	(XEMACPS_RXSR_HRESPNOK_MASK | \
					XEMACPS_RXSR_RXOVR_MASK | \
					XEMACPS_RXSR_BUFFNA_MASK)

/* interrupts bit definitions
 * Bits definitions are same in XEMACPS_ISR_OFFSET,
 * XEMACPS_IER_OFFSET, XEMACPS_IDR_OFFSET, and XEMACPS_IMR_OFFSET
 */
#define XEMACPS_IXR_PTPPSTX_MASK	0x02000000 /* PTP Psync transmitted */
#define XEMACPS_IXR_PTPPDRTX_MASK	0x01000000 /* PTP Pdelay_req
							transmitted */
#define XEMACPS_IXR_PTPSTX_MASK	0x00800000 /* PTP Sync transmitted */
#define XEMACPS_IXR_PTPDRTX_MASK	0x00400000 /* PTP Delay_req
							transmitted */
#define XEMACPS_IXR_PTPPSRX_MASK	0x00200000 /* PTP Psync received */
#define XEMACPS_IXR_PTPPDRRX_MASK	0x00100000 /* PTP Pdelay_req
							received */
#define XEMACPS_IXR_PTPSRX_MASK	0x00080000 /* PTP Sync received */
#define XEMACPS_IXR_PTPDRRX_MASK	0x00040000 /* PTP Delay_req received */
#define XEMACPS_IXR_PAUSETX_MASK	0x00004000 /* Pause frame
							transmitted */
#define XEMACPS_IXR_PAUSEZERO_MASK	0x00002000 /* Pause time has reached
							zero */
#define XEMACPS_IXR_PAUSENZERO_MASK	0x00001000 /* Pause frame received */
#define XEMACPS_IXR_HRESPNOK_MASK	0x00000800 /* hresp not ok */
#define XEMACPS_IXR_RXOVR_MASK		0x00000400 /* Receive overrun
							occurred */
#define XEMACPS_IXR_TXCOMPL_MASK	0x00000080 /* Frame transmitted ok */
#define XEMACPS_IXR_TXEXH_MASK		0x00000040 /* Transmit err occurred or
							no buffers*/
#define XEMACPS_IXR_RETRY_MASK		0x00000020 /* Retry limit exceeded */
#define XEMACPS_IXR_URUN_MASK		0x00000010 /* Transmit underrun */
#define XEMACPS_IXR_TXUSED_MASK	0x00000008 /* Tx buffer used bit read */
#define XEMACPS_IXR_RXUSED_MASK	0x00000004 /* Rx buffer used bit read */
#define XEMACPS_IXR_FRAMERX_MASK	0x00000002 /* Frame received ok */
#define XEMACPS_IXR_MGMNT_MASK		0x00000001 /* PHY management complete */
#define XEMACPS_IXR_ALL_MASK		0x03FC7FFE /* Everything except MDIO */

#define XEMACPS_IXR_TX_ERR_MASK	(XEMACPS_IXR_TXEXH_MASK |	\
					XEMACPS_IXR_RETRY_MASK |	\
					XEMACPS_IXR_URUN_MASK |		\
					XEMACPS_IXR_TXUSED_MASK)

#define XEMACPS_IXR_RX_ERR_MASK	(XEMACPS_IXR_HRESPNOK_MASK |	\
					XEMACPS_IXR_RXUSED_MASK |	\
					XEMACPS_IXR_RXOVR_MASK)
/* PHY Maintenance bit definitions */
#define XEMACPS_PHYMNTNC_OP_MASK	0x40020000 /* operation mask bits */
#define XEMACPS_PHYMNTNC_OP_R_MASK	0x20000000 /* read operation */
#define XEMACPS_PHYMNTNC_OP_W_MASK	0x10000000 /* write operation */
#define XEMACPS_PHYMNTNC_ADDR_MASK	0x0F800000 /* Address bits */
#define XEMACPS_PHYMNTNC_REG_MASK	0x007C0000 /* register bits */
#define XEMACPS_PHYMNTNC_DATA_MASK	0x0000FFFF /* data bits */
#define XEMACPS_PHYMNTNC_PHYAD_SHIFT_MASK	23 /* Shift bits for PHYAD */
#define XEMACPS_PHYMNTNC_PHREG_SHIFT_MASK	18 /* Shift bits for PHREG */

/* Wake on LAN bit definition */
#define XEMACPS_WOL_MCAST_MASK		0x00080000
#define XEMACPS_WOL_SPEREG1_MASK	0x00040000
#define XEMACPS_WOL_ARP_MASK		0x00020000
#define XEMACPS_WOL_MAGIC_MASK		0x00010000
#define XEMACPS_WOL_ARP_ADDR_MASK	0x0000FFFF

/* Buffer descriptor status words offset */
#define XEMACPS_BD_ADDR_OFFSET		0x00000000 /**< word 0/addr of BDs */
#define XEMACPS_BD_STAT_OFFSET		0x00000004 /**< word 1/status of BDs */

/* Transmit buffer descriptor status words bit positions.
 * Transmit buffer descriptor consists of two 32-bit registers,
 * the first - word0 contains a 32-bit address pointing to the location of
 * the transmit data.
 * The following register - word1, consists of various information to
 * control transmit process.  After transmit, this is updated with status
 * information, whether the frame was transmitted OK or why it had failed.
 */
#define XEMACPS_TXBUF_USED_MASK	0x80000000 /* Used bit. */
#define XEMACPS_TXBUF_WRAP_MASK	0x40000000 /* Wrap bit, last
							descriptor */
#define XEMACPS_TXBUF_RETRY_MASK	0x20000000 /* Retry limit exceeded */
#define XEMACPS_TXBUF_EXH_MASK		0x08000000 /* Buffers exhausted */
#define XEMACPS_TXBUF_LAC_MASK		0x04000000 /* Late collision. */
#define XEMACPS_TXBUF_NOCRC_MASK	0x00010000 /* No CRC */
#define XEMACPS_TXBUF_LAST_MASK	0x00008000 /* Last buffer */
#define XEMACPS_TXBUF_LEN_MASK		0x00003FFF /* Mask for length field */

#define XEMACPS_TXBUF_ERR_MASK		0x3C000000 /* Mask for length field */

/* Receive buffer descriptor status words bit positions.
 * Receive buffer descriptor consists of two 32-bit registers,
 * the first - word0 contains a 32-bit word aligned address pointing to the
 * address of the buffer. The lower two bits make up the wrap bit indicating
 * the last descriptor and the ownership bit to indicate it has been used.
 * The following register - word1, contains status information regarding why
 * the frame was received (the filter match condition) as well as other
 * useful info.
 */
#define XEMACPS_RXBUF_BCAST_MASK	0x80000000 /* Broadcast frame */
#define XEMACPS_RXBUF_MULTIHASH_MASK	0x40000000 /* Multicast hashed frame */
#define XEMACPS_RXBUF_UNIHASH_MASK	0x20000000 /* Unicast hashed frame */
#define XEMACPS_RXBUF_EXH_MASK		0x08000000 /* buffer exhausted */
#define XEMACPS_RXBUF_AMATCH_MASK	0x06000000 /* Specific address
						matched */
#define XEMACPS_RXBUF_IDFOUND_MASK	0x01000000 /* Type ID matched */
#define XEMACPS_RXBUF_IDMATCH_MASK	0x00C00000 /* ID matched mask */
#define XEMACPS_RXBUF_VLAN_MASK	0x00200000 /* VLAN tagged */
#define XEMACPS_RXBUF_PRI_MASK		0x00100000 /* Priority tagged */
#define XEMACPS_RXBUF_VPRI_MASK		0x000E0000 /* Vlan priority */
#define XEMACPS_RXBUF_CFI_MASK		0x00010000 /* CFI frame */
#define XEMACPS_RXBUF_EOF_MASK		0x00008000 /* End of frame. */
#define XEMACPS_RXBUF_SOF_MASK		0x00004000 /* Start of frame. */
#define XEMACPS_RXBUF_LEN_MASK		0x00003FFF /* Mask for length field */

#define XEMACPS_RXBUF_WRAP_MASK	0x00000002 /* Wrap bit, last BD */
#define XEMACPS_RXBUF_NEW_MASK		0x00000001 /* Used bit.. */
#define XEMACPS_RXBUF_ADD_MASK		0xFFFFFFFC /* Mask for address */

#define XEAMCPS_GEN_PURPOSE_TIMER_LOAD	100 /* timeout value is msecs */

#define XEMACPS_GMII2RGMII_FULLDPLX		BMCR_FULLDPLX
#define XEMACPS_GMII2RGMII_SPEED1000		BMCR_SPEED1000
#define XEMACPS_GMII2RGMII_SPEED100		BMCR_SPEED100
#define XEMACPS_GMII2RGMII_REG_NUM			0x10

#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
#define NS_PER_SEC			1000000000ULL /* Nanoseconds per
							second */
#endif

#define xemacps_read(base, reg)					\
	__raw_readl(((void __iomem *)(base)) + (reg))
#define xemacps_write(base, reg, val)					\
	__raw_writel((val), ((void __iomem *)(base)) + (reg))

struct ring_info {
	struct sk_buff *skb;
	dma_addr_t mapping;
	size_t len;
};

/* DMA buffer descriptor structure. Each BD is two words */
struct xemacps_bd {
	u32 addr;
	u32 ctrl;
};


/* Our private device data. */
struct net_local {
	void __iomem *baseaddr;
	struct clk *devclk;
	struct clk *aperclk;
	struct notifier_block clk_rate_change_nb;

	struct device_node *phy_node;
	struct device_node *gmii2rgmii_phy_node;
	struct ring_info *tx_skb;
	struct ring_info *rx_skb;

	struct xemacps_bd *rx_bd;
	struct xemacps_bd *tx_bd;

	dma_addr_t rx_bd_dma; /* physical address */
	dma_addr_t tx_bd_dma; /* physical address */

	u32 tx_bd_ci;
	u32 tx_bd_tail;
	u32 rx_bd_ci;

	u32 tx_bd_freecnt;

	spinlock_t tx_lock;
	spinlock_t rx_lock;
	spinlock_t nwctrlreg_lock;

	struct platform_device *pdev;
	struct net_device *ndev; /* this device */
	struct tasklet_struct tx_bdreclaim_tasklet;
	struct workqueue_struct *txtimeout_handler_wq;
	struct work_struct txtimeout_reinit;

	struct napi_struct napi; /* napi information for device */
	struct net_device_stats stats; /* Statistics for this device */

	struct timer_list gen_purpose_timer; /* Used for stats update */

	/* Manage internal timer for packet timestamping */
	struct cyclecounter cycles;
	struct timecounter clock;
	struct hwtstamp_config hwtstamp_config;

	struct mii_bus *mii_bus;
	struct phy_device *phy_dev;
	struct phy_device *gmii2rgmii_phy_dev;
	phy_interface_t phy_interface;
	unsigned int link;
	unsigned int speed;
	unsigned int duplex;
	/* RX ip/tcp/udp checksum */
	unsigned ip_summed;
	unsigned int enetnum;
	unsigned int lastrxfrmscntr;
#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
	unsigned int ptpenetclk;
#endif
};
#define to_net_local(_nb)	container_of(_nb, struct net_local,\
		clk_rate_change_nb)

static struct net_device_ops netdev_ops;

/**
 * xemacps_mdio_read - Read current value of phy register indicated by
 * phyreg.
 * @bus: mdio bus
 * @mii_id: mii id
 * @phyreg: phy register to be read
 *
 * @return: value read from specified phy register.
 *
 * note: This is for 802.3 clause 22 phys access. For 802.3 clause 45 phys
 * access, set bit 30 to be 1. e.g. change XEMACPS_PHYMNTNC_OP_MASK to
 * 0x00020000.
 */
static int xemacps_mdio_read(struct mii_bus *bus, int mii_id, int phyreg)
{
	struct net_local *lp = bus->priv;
	u32 regval;
	int value;
	volatile u32 ipisr;

	regval  = XEMACPS_PHYMNTNC_OP_MASK;
	regval |= XEMACPS_PHYMNTNC_OP_R_MASK;
	regval |= (mii_id << XEMACPS_PHYMNTNC_PHYAD_SHIFT_MASK);
	regval |= (phyreg << XEMACPS_PHYMNTNC_PHREG_SHIFT_MASK);

	xemacps_write(lp->baseaddr, XEMACPS_PHYMNTNC_OFFSET, regval);

	/* wait for end of transfer */
	do {
		cpu_relax();
		ipisr = xemacps_read(lp->baseaddr, XEMACPS_NWSR_OFFSET);
	} while ((ipisr & XEMACPS_NWSR_MDIOIDLE_MASK) == 0);

	value = xemacps_read(lp->baseaddr, XEMACPS_PHYMNTNC_OFFSET) &
			XEMACPS_PHYMNTNC_DATA_MASK;

	return value;
}

/**
 * xemacps_mdio_write - Write passed in value to phy register indicated
 * by phyreg.
 * @bus: mdio bus
 * @mii_id: mii id
 * @phyreg: phy register to be configured.
 * @value: value to be written to phy register.
 * return 0. This API requires to be int type or compile warning generated
 *
 * note: This is for 802.3 clause 22 phys access. For 802.3 clause 45 phys
 * access, set bit 30 to be 1. e.g. change XEMACPS_PHYMNTNC_OP_MASK to
 * 0x00020000.
 */
static int xemacps_mdio_write(struct mii_bus *bus, int mii_id, int phyreg,
	u16 value)
{
	struct net_local *lp = bus->priv;
	u32 regval;
	volatile u32 ipisr;

	regval  = XEMACPS_PHYMNTNC_OP_MASK;
	regval |= XEMACPS_PHYMNTNC_OP_W_MASK;
	regval |= (mii_id << XEMACPS_PHYMNTNC_PHYAD_SHIFT_MASK);
	regval |= (phyreg << XEMACPS_PHYMNTNC_PHREG_SHIFT_MASK);
	regval |= value;

	xemacps_write(lp->baseaddr, XEMACPS_PHYMNTNC_OFFSET, regval);

	/* wait for end of transfer */
	do {
		cpu_relax();
		ipisr = xemacps_read(lp->baseaddr, XEMACPS_NWSR_OFFSET);
	} while ((ipisr & XEMACPS_NWSR_MDIOIDLE_MASK) == 0);

	return 0;
}


/**
 * xemacps_mdio_reset - mdio reset. It seems to be required per open
 * source documentation phy.txt. But there is no reset in this device.
 * Provide function API for now.
 * @bus: mdio bus
 **/
static int xemacps_mdio_reset(struct mii_bus *bus)
{
	return 0;
}

/**
 * xemacps_set_freq() - Set a clock to a new frequency
 * @clk		Pointer to the clock to change
 * @rate	New frequency in Hz
 * @dev		Pointer to the struct device
 */
static void xemacps_set_freq(struct clk *clk, long rate, struct device *dev)
{
	rate = clk_round_rate(clk, rate);
	if (rate < 0)
		return;

	dev_info(dev, "Set clk to %ld Hz\n", rate);
	if (clk_set_rate(clk, rate))
		dev_err(dev, "Setting new clock rate failed.\n");
}

/**
 * xemacps_adjust_link - handles link status changes, such as speed,
 * duplex, up/down, ...
 * @ndev: network device
 */
static void xemacps_adjust_link(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = lp->phy_dev;
	struct phy_device *gmii2rgmii_phydev = lp->gmii2rgmii_phy_dev;
	int status_change = 0;
	u32 regval;
	u16 gmii2rgmii_reg = 0;

	if (phydev->link) {
		if ((lp->speed != phydev->speed) ||
			(lp->duplex != phydev->duplex)) {
			regval = xemacps_read(lp->baseaddr,
				XEMACPS_NWCFG_OFFSET);
			regval &= ~(XEMACPS_NWCFG_FDEN_MASK |
					XEMACPS_NWCFG_1000_MASK |
					XEMACPS_NWCFG_100_MASK);

			if (phydev->duplex) {
				regval |= XEMACPS_NWCFG_FDEN_MASK;
				gmii2rgmii_reg |= XEMACPS_GMII2RGMII_FULLDPLX;
			}

			if (phydev->speed == SPEED_1000) {
				regval |= XEMACPS_NWCFG_1000_MASK;
				gmii2rgmii_reg |= XEMACPS_GMII2RGMII_SPEED1000;
				xemacps_set_freq(lp->devclk, 125000000,
						&lp->pdev->dev);
			} else if (phydev->speed == SPEED_100) {
				regval |= XEMACPS_NWCFG_100_MASK;
				gmii2rgmii_reg |= XEMACPS_GMII2RGMII_SPEED100;
				xemacps_set_freq(lp->devclk, 25000000,
						&lp->pdev->dev);
			} else if (phydev->speed == SPEED_10) {
				xemacps_set_freq(lp->devclk, 2500000,
						&lp->pdev->dev);
			} else {
				dev_err(&lp->pdev->dev,
					"%s: unknown PHY speed %d\n",
					__func__, phydev->speed);
				return;
			}

			xemacps_write(lp->baseaddr, XEMACPS_NWCFG_OFFSET,
			regval);

			if (gmii2rgmii_phydev != NULL) {
				xemacps_mdio_write(lp->mii_bus,
					gmii2rgmii_phydev->addr,
					XEMACPS_GMII2RGMII_REG_NUM,
					gmii2rgmii_reg);
			}

			lp->speed = phydev->speed;
			lp->duplex = phydev->duplex;
			status_change = 1;
		}
	}

	if (phydev->link != lp->link) {
		lp->link = phydev->link;
		status_change = 1;
	}

	if (status_change) {
		if (phydev->link)
			dev_info(&lp->pdev->dev, "link up (%d/%s)\n",
				phydev->speed,
				DUPLEX_FULL == phydev->duplex ?
				"FULL" : "HALF");
		else
			dev_info(&lp->pdev->dev, "link down\n");
	}
}

static int xemacps_clk_notifier_cb(struct notifier_block *nb, unsigned long
		event, void *data)
{
/*
	struct clk_notifier_data *ndata = data;
	struct net_local *nl = to_net_local(nb);
*/

	switch (event) {
	case PRE_RATE_CHANGE:
		/* if a rate change is announced we need to check whether we
		 * can maintain the current frequency by changing the clock
		 * dividers.
		 * I don't see how this can be done using the current fmwk!?
		 * For now we always allow the rate change. Otherwise we would
		 * even prevent ourself to change the rate.
		 */
		return NOTIFY_OK;
	case POST_RATE_CHANGE:
		/* not sure this will work. actually i'm sure it does not. this
		 * callback is not allowed to call back into COMMON_CLK, what
		 * adjust_link() does...
		 */
		/*xemacps_adjust_link(nl->ndev); would likely lock up kernel */
		return NOTIFY_OK;
	case ABORT_RATE_CHANGE:
	default:
		return NOTIFY_DONE;
	}
}

/**
 * xemacps_mii_probe - probe mii bus, find the right bus_id to register
 * phy callback function.
 * @ndev: network interface device structure
 * return 0 on success, negative value if error
 **/
static int xemacps_mii_probe(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = NULL;

	if (lp->phy_node) {
		phydev = of_phy_connect(lp->ndev,
					lp->phy_node,
					&xemacps_adjust_link,
					0,
					lp->phy_interface);
	}
	if (!phydev) {
		dev_err(&lp->pdev->dev, "%s: no PHY found\n", ndev->name);
		return -1;
	}

	dev_dbg(&lp->pdev->dev,
		"GEM: phydev %p, phydev->phy_id 0x%x, phydev->addr 0x%x\n",
		phydev, phydev->phy_id, phydev->addr);

	phydev->supported &= (PHY_GBIT_FEATURES | SUPPORTED_Pause |
							SUPPORTED_Asym_Pause);
	phydev->advertising = phydev->supported;

	lp->link    = 0;
	lp->speed   = 0;
	lp->duplex  = -1;
	lp->phy_dev = phydev;

	phy_start(lp->phy_dev);

	dev_dbg(&lp->pdev->dev, "phy_addr 0x%x, phy_id 0x%08x\n",
			lp->phy_dev->addr, lp->phy_dev->phy_id);

	dev_dbg(&lp->pdev->dev, "attach [%s] phy driver\n",
			lp->phy_dev->drv->name);

	if (lp->gmii2rgmii_phy_node) {
		phydev = of_phy_connect(lp->ndev,
					lp->gmii2rgmii_phy_node,
					NULL,
					0, 0);
		if (!phydev) {
			dev_err(&lp->pdev->dev,
				"%s: no gmii to rgmii converter found\n",
			ndev->name);
			return -1;
		}
		lp->gmii2rgmii_phy_dev = phydev;
	} else
		lp->gmii2rgmii_phy_dev = NULL;

	return 0;
}

/**
 * xemacps_mii_init - Initialize and register mii bus to network device
 * @lp: local device instance pointer
 * return 0 on success, negative value if error
 **/
static int xemacps_mii_init(struct net_local *lp)
{
	int rc = -ENXIO, i;
	struct resource res;
	struct device_node *np = of_get_parent(lp->phy_node);
	struct device_node *npp;

	lp->mii_bus = mdiobus_alloc();
	if (lp->mii_bus == NULL) {
		rc = -ENOMEM;
		goto err_out;
	}

	lp->mii_bus->name  = "XEMACPS mii bus";
	lp->mii_bus->read  = &xemacps_mdio_read;
	lp->mii_bus->write = &xemacps_mdio_write;
	lp->mii_bus->reset = &xemacps_mdio_reset;
	lp->mii_bus->priv = lp;
	lp->mii_bus->parent = &lp->ndev->dev;

	lp->mii_bus->irq = kmalloc(sizeof(int)*PHY_MAX_ADDR, GFP_KERNEL);
	if (!lp->mii_bus->irq) {
		rc = -ENOMEM;
		goto err_out_free_mdiobus;
	}

	for (i = 0; i < PHY_MAX_ADDR; i++)
		lp->mii_bus->irq[i] = PHY_POLL;
	npp = of_get_parent(np);
	of_address_to_resource(npp, 0, &res);
	snprintf(lp->mii_bus->id, MII_BUS_ID_SIZE, "%.8llx",
		 (unsigned long long)res.start);
	if (of_mdiobus_register(lp->mii_bus, np))
		goto err_out_free_mdio_irq;

	return 0;

err_out_free_mdio_irq:
	kfree(lp->mii_bus->irq);
err_out_free_mdiobus:
	mdiobus_free(lp->mii_bus);
err_out:
	return rc;
}

/**
 * xemacps_update_hdaddr - Update device's MAC address when configured
 * MAC address is not valid, reconfigure with a good one.
 * @lp: local device instance pointer
 **/
static void xemacps_update_hwaddr(struct net_local *lp)
{
	u32 regvall;
	u16 regvalh;
	u8  addr[6];

	regvall = xemacps_read(lp->baseaddr, XEMACPS_LADDR1L_OFFSET);
	regvalh = xemacps_read(lp->baseaddr, XEMACPS_LADDR1H_OFFSET);
	addr[0] = regvall & 0xFF;
	addr[1] = (regvall >> 8) & 0xFF;
	addr[2] = (regvall >> 16) & 0xFF;
	addr[3] = (regvall >> 24) & 0xFF;
	addr[4] = regvalh & 0xFF;
	addr[5] = (regvalh >> 8) & 0xFF;

	if (is_valid_ether_addr(addr)) {
		memcpy(lp->ndev->dev_addr, addr, sizeof(addr));
	} else {
		dev_info(&lp->pdev->dev, "invalid address, use assigned\n");
		random_ether_addr(lp->ndev->dev_addr);
		dev_info(&lp->pdev->dev,
				"MAC updated %02x:%02x:%02x:%02x:%02x:%02x\n",
				lp->ndev->dev_addr[0], lp->ndev->dev_addr[1],
				lp->ndev->dev_addr[2], lp->ndev->dev_addr[3],
				lp->ndev->dev_addr[4], lp->ndev->dev_addr[5]);
	}
}

/**
 * xemacps_set_hwaddr - Set device's MAC address from ndev->dev_addr
 * @lp: local device instance pointer
 **/
static void xemacps_set_hwaddr(struct net_local *lp)
{
	u32 regvall = 0;
	u16 regvalh = 0;
#ifdef __LITTLE_ENDIAN
	regvall = cpu_to_le32(*((u32 *)lp->ndev->dev_addr));
	regvalh = cpu_to_le16(*((u16 *)(lp->ndev->dev_addr + 4)));
#endif
#ifdef __BIG_ENDIAN
	regvall = cpu_to_be32(*((u32 *)lp->ndev->dev_addr));
	regvalh = cpu_to_be16(*((u16 *)(lp->ndev->dev_addr + 4)));
#endif
	/* LADDRXH has to be wriiten latter than LADDRXL to enable
	 * this address even if these 16 bits are zeros.
	 */
	xemacps_write(lp->baseaddr, XEMACPS_LADDR1L_OFFSET, regvall);
	xemacps_write(lp->baseaddr, XEMACPS_LADDR1H_OFFSET, regvalh);
#ifdef DEBUG
	regvall = xemacps_read(lp->baseaddr, XEMACPS_LADDR1L_OFFSET);
	regvalh = xemacps_read(lp->baseaddr, XEMACPS_LADDR1H_OFFSET);
	dev_dbg(&lp->pdev->dev,
			"MAC 0x%08x, 0x%08x, %02x:%02x:%02x:%02x:%02x:%02x\n",
		regvall, regvalh,
		(regvall & 0xff), ((regvall >> 8) & 0xff),
		((regvall >> 16) & 0xff), (regvall >> 24),
		(regvalh & 0xff), (regvalh >> 8));
#endif
}

/**
 * xemacps_reset_hw - Helper function to reset the underlying hardware.
 * This is called when we get into such deep trouble that we don't know
 * how to handle otherwise.
 * @lp: local device instance pointer
 */
static void xemacps_reset_hw(struct net_local *lp)
{
	u32 regisr;
	/* make sure we have the buffer for ourselves */
	wmb();

	/* Have a clean start */
	xemacps_write(lp->baseaddr, XEMACPS_NWCTRL_OFFSET, 0);

	/* Clear statistic counters */
	xemacps_write(lp->baseaddr, XEMACPS_NWCTRL_OFFSET,
		XEMACPS_NWCTRL_STATCLR_MASK);

	/* Clear TX and RX status */
	xemacps_write(lp->baseaddr, XEMACPS_TXSR_OFFSET, ~0UL);
	xemacps_write(lp->baseaddr, XEMACPS_RXSR_OFFSET, ~0UL);

	/* Disable all interrupts */
	xemacps_write(lp->baseaddr, XEMACPS_IDR_OFFSET, ~0UL);
	synchronize_irq(lp->ndev->irq);
	regisr = xemacps_read(lp->baseaddr, XEMACPS_ISR_OFFSET);
	xemacps_write(lp->baseaddr, XEMACPS_ISR_OFFSET, regisr);
}

#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP

/**
 * xemacps_get_hwticks - get the current value of the GEM internal timer
 * @lp: local device instance pointer
 * return: nothing
 **/
static inline void
xemacps_get_hwticks(struct net_local *lp, u64 *sec, u64 *nsec)
{
	do {
		*nsec = xemacps_read(lp->baseaddr, XEMACPS_1588NS_OFFSET);
		*sec = xemacps_read(lp->baseaddr, XEMACPS_1588S_OFFSET);
	} while (*nsec > xemacps_read(lp->baseaddr, XEMACPS_1588NS_OFFSET));
}

/**
 * xemacps_read_clock - read raw cycle counter (to be used by time counter)
 */
static cycle_t xemacps_read_clock(const struct cyclecounter *tc)
{
	struct net_local *lp =
			container_of(tc, struct net_local, cycles);
	u64 stamp;
	u64 sec, nsec;

	xemacps_get_hwticks(lp, &sec, &nsec);
	stamp = (sec << 32) | nsec;

	return stamp;
}


/**
 * xemacps_systim_to_hwtstamp - convert system time value to hw timestamp
 * @adapter: board private structure
 * @shhwtstamps: timestamp structure to update
 * @regval: unsigned 64bit system time value.
 *
 * We need to convert the system time value stored in the RX/TXSTMP registers
 * into a hwtstamp which can be used by the upper level timestamping functions
 */
static void xemacps_systim_to_hwtstamp(struct net_local *lp,
				struct skb_shared_hwtstamps *shhwtstamps,
				u64 regval)
{
	u64 ns;

	ns = timecounter_cyc2time(&lp->clock, regval);
	timecompare_update(&lp->compare, ns);
	memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));
	shhwtstamps->hwtstamp = ns_to_ktime(ns);
	shhwtstamps->syststamp = timecompare_transform(&lp->compare, ns);
}

static void
xemacps_rx_hwtstamp(struct net_local *lp,
			struct sk_buff *skb, unsigned msg_type)
{
	u64 time64, sec, nsec;

	if (!msg_type) {
		/* PTP Event Frame packets */
		sec = xemacps_read(lp->baseaddr, XEMACPS_PTPERXS_OFFSET);
		nsec = xemacps_read(lp->baseaddr, XEMACPS_PTPERXNS_OFFSET);
	} else {
		/* PTP Peer Event Frame packets */
		sec = xemacps_read(lp->baseaddr, XEMACPS_PTPPRXS_OFFSET);
		nsec = xemacps_read(lp->baseaddr, XEMACPS_PTPPRXNS_OFFSET);
	}
	time64 = (sec << 32) | nsec;
	xemacps_systim_to_hwtstamp(lp, skb_hwtstamps(skb), time64);
}

static void
xemacps_tx_hwtstamp(struct net_local *lp,
			struct sk_buff *skb, unsigned msg_type)
{
	u64 time64, sec, nsec;

	if (!msg_type) {
		/* PTP Event Frame packets */
		sec = xemacps_read(lp->baseaddr, XEMACPS_PTPETXS_OFFSET);
		nsec = xemacps_read(lp->baseaddr, XEMACPS_PTPETXNS_OFFSET);
	} else {
		/* PTP Peer Event Frame packets */
		sec = xemacps_read(lp->baseaddr, XEMACPS_PTPPTXS_OFFSET);
		nsec = xemacps_read(lp->baseaddr, XEMACPS_PTPPTXNS_OFFSET);
	}

	time64 = (sec << 32) | nsec;
	xemacps_systim_to_hwtstamp(lp, skb_hwtstamps(skb), time64);
	skb_tstamp_tx(skb, skb_hwtstamps(skb));
}

#endif /* CONFIG_XILINX_PS_EMAC_HWTSTAMP */

/**
 * xemacps_rx - process received packets when napi called
 * @lp: local device instance pointer
 * @budget: NAPI budget
 * return: number of BDs processed
 **/
static int xemacps_rx(struct net_local *lp, int budget)
{
	struct xemacps_bd *cur_p;
	u32 len;
	struct sk_buff *skb;
	struct sk_buff *new_skb;
	u32 new_skb_baddr;
	unsigned int numbdfree = 0;
	u32 size = 0;
	u32 packets = 0;
	u32 regval;

	cur_p = &lp->rx_bd[lp->rx_bd_ci];
	regval = cur_p->addr;
	rmb();
	while (numbdfree < budget) {
		if (!(regval & XEMACPS_RXBUF_NEW_MASK))
			break;

		new_skb = netdev_alloc_skb(lp->ndev, XEMACPS_RX_BUF_SIZE);
		if (new_skb == NULL) {
			dev_err(&lp->ndev->dev, "no memory for new sk_buff\n");
			break;
		}
		/* Get dma handle of skb->data */
		new_skb_baddr = (u32) dma_map_single(lp->ndev->dev.parent,
					new_skb->data,
					XEMACPS_RX_BUF_SIZE,
					DMA_FROM_DEVICE);

		/* the packet length */
		len = cur_p->ctrl & XEMACPS_RXBUF_LEN_MASK;
		rmb();
		skb = lp->rx_skb[lp->rx_bd_ci].skb;
		dma_unmap_single(lp->ndev->dev.parent,
				lp->rx_skb[lp->rx_bd_ci].mapping,
				lp->rx_skb[lp->rx_bd_ci].len,
				DMA_FROM_DEVICE);

		/* setup received skb and send it upstream */
		skb_put(skb, len);  /* Tell the skb how much data we got. */
		skb->protocol = eth_type_trans(skb, lp->ndev);

		skb->ip_summed = lp->ip_summed;

#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
		if ((lp->hwtstamp_config.rx_filter == HWTSTAMP_FILTER_ALL) &&
		    (ntohs(skb->protocol) == 0x800)) {
			unsigned ip_proto, dest_port, msg_type;

			/* While the GEM can timestamp PTP packets, it does
			 * not mark the RX descriptor to identify them.  This
			 * is entirely the wrong place to be parsing UDP
			 * headers, but some minimal effort must be made.
			 * NOTE: the below parsing of ip_proto and dest_port
			 * depend on the use of Ethernet_II encapsulation,
			 * IPv4 without any options.
			 */
			ip_proto = *((u8 *)skb->mac_header + 14 + 9);
			dest_port = ntohs(*(((u16 *)skb->mac_header) +
						((14 + 20 + 2)/2)));
			msg_type = *((u8 *)skb->mac_header + 42);
			if ((ip_proto == IPPROTO_UDP) &&
			    (dest_port == 0x13F)) {
				/* Timestamp this packet */
				xemacps_rx_hwtstamp(lp, skb, msg_type & 0x2);
			}
		}
#endif /* CONFIG_XILINX_PS_EMAC_HWTSTAMP */
		size += len;
		packets++;
		netif_receive_skb(skb);

		cur_p->addr = (cur_p->addr & ~XEMACPS_RXBUF_ADD_MASK)
					| (new_skb_baddr);
		lp->rx_skb[lp->rx_bd_ci].skb = new_skb;
		lp->rx_skb[lp->rx_bd_ci].mapping = new_skb_baddr;
		lp->rx_skb[lp->rx_bd_ci].len = XEMACPS_RX_BUF_SIZE;

		cur_p->ctrl = 0;
		cur_p->addr &= (~XEMACPS_RXBUF_NEW_MASK);
		wmb();

		lp->rx_bd_ci++;
		lp->rx_bd_ci = lp->rx_bd_ci % XEMACPS_RECV_BD_CNT;
		cur_p = &lp->rx_bd[lp->rx_bd_ci];
		regval = cur_p->addr;
		rmb();
		numbdfree++;
	}
	wmb();
	lp->stats.rx_packets += packets;
	lp->stats.rx_bytes += size;
	return numbdfree;
}

/**
 * xemacps_rx_poll - NAPI poll routine
 * napi: pointer to napi struct
 * budget:
 **/
static int xemacps_rx_poll(struct napi_struct *napi, int budget)
{
	struct net_local *lp = container_of(napi, struct net_local, napi);
	int work_done = 0;
	u32 regval;

	spin_lock(&lp->rx_lock);
	while (1) {
		regval = xemacps_read(lp->baseaddr, XEMACPS_RXSR_OFFSET);
		xemacps_write(lp->baseaddr, XEMACPS_RXSR_OFFSET, regval);
		if (regval & XEMACPS_RXSR_HRESPNOK_MASK)
			dev_err(&lp->pdev->dev, "RX error 0x%x\n", regval);

		work_done += xemacps_rx(lp, budget - work_done);
		if (work_done >= budget)
			break;

		napi_complete(napi);
		/* We disabled RX interrupts in interrupt service
		 * routine, now it is time to enable it back.
		 */
		xemacps_write(lp->baseaddr,
			XEMACPS_IER_OFFSET, XEMACPS_IXR_FRAMERX_MASK);

		/* If a packet has come in between the last check of the BD
		 * list and unmasking the interrupts, we may have missed the
		 * interrupt, so reschedule here.
		 */
		if ((lp->rx_bd[lp->rx_bd_ci].addr & XEMACPS_RXBUF_NEW_MASK)
		&&  napi_reschedule(napi)) {
			xemacps_write(lp->baseaddr,
				XEMACPS_IDR_OFFSET, XEMACPS_IXR_FRAMERX_MASK);
			continue;
		}
		break;
	}
	spin_unlock(&lp->rx_lock);
	return work_done;
}

/**
 * xemacps_tx_poll - tx bd reclaim tasklet handler
 * @data: pointer to network interface device structure
 **/
static void xemacps_tx_poll(unsigned long data)
{
	struct net_device *ndev = (struct net_device *)data;
	struct net_local *lp = netdev_priv(ndev);
	u32 regval;
	u32 len = 0;
	unsigned int bdcount = 0;
	unsigned int bdpartialcount = 0;
	unsigned int sop = 0;
	struct xemacps_bd *cur_p;
	u32 cur_i;
	u32 numbdstofree;
	u32 numbdsinhw;
	struct ring_info *rp;
	struct sk_buff *skb;
	unsigned long flags;

	spin_lock(&lp->tx_lock);
	regval = xemacps_read(lp->baseaddr, XEMACPS_TXSR_OFFSET);
	xemacps_write(lp->baseaddr, XEMACPS_TXSR_OFFSET, regval);
	dev_dbg(&lp->pdev->dev, "TX status 0x%x\n", regval);
	if (regval & (XEMACPS_TXSR_HRESPNOK_MASK | XEMACPS_TXSR_BUFEXH_MASK))
		dev_err(&lp->pdev->dev, "TX error 0x%x\n", regval);

	cur_i = lp->tx_bd_ci;
	cur_p = &lp->tx_bd[cur_i];
	numbdsinhw = XEMACPS_SEND_BD_CNT - lp->tx_bd_freecnt;
	while (bdcount < numbdsinhw) {
		if (sop == 0) {
			if (cur_p->ctrl & XEMACPS_TXBUF_USED_MASK)
				sop = 1;
			else
				break;
		}

		bdcount++;
		bdpartialcount++;

		/* hardware has processed this BD so check the "last" bit.
		 * If it is clear, then there are more BDs for the current
		 * packet. Keep a count of these partial packet BDs.
		 */
		if (cur_p->ctrl & XEMACPS_TXBUF_LAST_MASK) {
			sop = 0;
			bdpartialcount = 0;
		}

		cur_i++;
		cur_i = cur_i % XEMACPS_SEND_BD_CNT;
		cur_p = &lp->tx_bd[cur_i];
	}
	numbdstofree = bdcount - bdpartialcount;
	lp->tx_bd_freecnt += numbdstofree;
	numbdsinhw -= numbdstofree;
	if (!numbdstofree)
		goto tx_poll_out;

	cur_p = &lp->tx_bd[lp->tx_bd_ci];
	while (numbdstofree) {
		rp = &lp->tx_skb[lp->tx_bd_ci];
		skb = rp->skb;

		len += (cur_p->ctrl & XEMACPS_TXBUF_LEN_MASK);

#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
		if ((lp->hwtstamp_config.tx_type == HWTSTAMP_TX_ON) &&
			(ntohs(skb->protocol) == 0x800)) {
			unsigned ip_proto, dest_port, msg_type;

			skb_reset_mac_header(skb);

			ip_proto = *((u8 *)skb->mac_header + 14 + 9);
			dest_port = ntohs(*(((u16 *)skb->mac_header) +
					((14 + 20 + 2)/2)));
			msg_type = *((u8 *)skb->mac_header + 42);
			if ((ip_proto == IPPROTO_UDP) &&
				(dest_port == 0x13F)) {
				/* Timestamp this packet */
				xemacps_tx_hwtstamp(lp, skb, msg_type & 0x2);
			}
		}
#endif /* CONFIG_XILINX_PS_EMAC_HWTSTAMP */

		dma_unmap_single(&lp->pdev->dev, rp->mapping, rp->len,
			DMA_TO_DEVICE);
		rp->skb = NULL;
		dev_kfree_skb(skb);
		/* log tx completed packets and bytes, errors logs
		 * are in other error counters.
		 */
		if (cur_p->ctrl & XEMACPS_TXBUF_LAST_MASK) {
			lp->stats.tx_packets++;
			lp->stats.tx_bytes += len;
			len = 0;
		}

		/* Set used bit, preserve wrap bit; clear everything else. */
		cur_p->ctrl |= XEMACPS_TXBUF_USED_MASK;
		cur_p->ctrl &= (XEMACPS_TXBUF_USED_MASK |
					XEMACPS_TXBUF_WRAP_MASK);

		lp->tx_bd_ci++;
		lp->tx_bd_ci = lp->tx_bd_ci % XEMACPS_SEND_BD_CNT;
		cur_p = &lp->tx_bd[lp->tx_bd_ci];
		numbdstofree--;
	}
	wmb();

	if (numbdsinhw) {
		spin_lock_irqsave(&lp->nwctrlreg_lock, flags);
		regval = xemacps_read(lp->baseaddr, XEMACPS_NWCTRL_OFFSET);
		regval |= XEMACPS_NWCTRL_STARTTX_MASK;
		xemacps_write(lp->baseaddr, XEMACPS_NWCTRL_OFFSET, regval);
		spin_unlock_irqrestore(&lp->nwctrlreg_lock, flags);
	}

	netif_wake_queue(ndev);

tx_poll_out:
	spin_unlock(&lp->tx_lock);
}

/**
 * xemacps_interrupt - interrupt main service routine
 * @irq: interrupt number
 * @dev_id: pointer to a network device structure
 * return IRQ_HANDLED or IRQ_NONE
 **/
static irqreturn_t xemacps_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct net_local *lp = netdev_priv(ndev);
	u32 regisr;
	u32 regctrl;

	regisr = xemacps_read(lp->baseaddr, XEMACPS_ISR_OFFSET);
	if (unlikely(!regisr))
		return IRQ_NONE;

	xemacps_write(lp->baseaddr, XEMACPS_ISR_OFFSET, regisr);

	while (regisr) {
		if (regisr & (XEMACPS_IXR_TXCOMPL_MASK |
				XEMACPS_IXR_TX_ERR_MASK)) {
			tasklet_schedule(&lp->tx_bdreclaim_tasklet);
		}

		if (regisr & XEMACPS_IXR_RXUSED_MASK) {
			spin_lock(&lp->nwctrlreg_lock);
			regctrl = xemacps_read(lp->baseaddr,
					XEMACPS_NWCTRL_OFFSET);
			regctrl |= XEMACPS_NWCTRL_FLUSH_DPRAM_MASK;
			xemacps_write(lp->baseaddr,
					XEMACPS_NWCTRL_OFFSET, regctrl);
			spin_unlock(&lp->nwctrlreg_lock);
		}

		if (regisr & XEMACPS_IXR_FRAMERX_MASK) {
			xemacps_write(lp->baseaddr,
				XEMACPS_IDR_OFFSET, XEMACPS_IXR_FRAMERX_MASK);
			napi_schedule(&lp->napi);
		}
		regisr = xemacps_read(lp->baseaddr, XEMACPS_ISR_OFFSET);
		xemacps_write(lp->baseaddr, XEMACPS_ISR_OFFSET, regisr);
	}

	return IRQ_HANDLED;
}

/* Free all packets presently in the descriptor rings. */
static void xemacps_clean_rings(struct net_local *lp)
{
	int i;

	for (i = 0; i < XEMACPS_RECV_BD_CNT; i++) {
		if (lp->rx_skb && lp->rx_skb[i].skb) {
			dma_unmap_single(lp->ndev->dev.parent,
					 lp->rx_skb[i].mapping,
					 lp->rx_skb[i].len,
					 DMA_FROM_DEVICE);

			dev_kfree_skb(lp->rx_skb[i].skb);
			lp->rx_skb[i].skb = NULL;
			lp->rx_skb[i].mapping = 0;
		}
	}

	for (i = 0; i < XEMACPS_SEND_BD_CNT; i++) {
		if (lp->tx_skb && lp->tx_skb[i].skb) {
			dma_unmap_single(lp->ndev->dev.parent,
					 lp->tx_skb[i].mapping,
					 lp->tx_skb[i].len,
					 DMA_TO_DEVICE);

			dev_kfree_skb(lp->tx_skb[i].skb);
			lp->tx_skb[i].skb = NULL;
			lp->tx_skb[i].mapping = 0;
		}
	}
}

/**
 * xemacps_descriptor_free - Free allocated TX and RX BDs
 * @lp: local device instance pointer
 **/
static void xemacps_descriptor_free(struct net_local *lp)
{
	int size;

	xemacps_clean_rings(lp);

	/* kfree(NULL) is safe, no need to check here */
	kfree(lp->tx_skb);
	lp->tx_skb = NULL;
	kfree(lp->rx_skb);
	lp->rx_skb = NULL;

	size = XEMACPS_RECV_BD_CNT * sizeof(struct xemacps_bd);
	if (lp->rx_bd) {
		dma_free_coherent(&lp->pdev->dev, size,
			lp->rx_bd, lp->rx_bd_dma);
		lp->rx_bd = NULL;
	}

	size = XEMACPS_SEND_BD_CNT * sizeof(struct xemacps_bd);
	if (lp->tx_bd) {
		dma_free_coherent(&lp->pdev->dev, size,
			lp->tx_bd, lp->tx_bd_dma);
		lp->tx_bd = NULL;
	}
}

/**
 * xemacps_descriptor_init - Allocate both TX and RX BDs
 * @lp: local device instance pointer
 * return 0 on success, negative value if error
 **/
static int xemacps_descriptor_init(struct net_local *lp)
{
	int size;
	struct sk_buff *new_skb;
	u32 new_skb_baddr;
	u32 i;
	struct xemacps_bd *cur_p;
	u32 regval;

	lp->tx_skb = NULL;
	lp->rx_skb = NULL;
	lp->rx_bd = NULL;
	lp->tx_bd = NULL;

	/* Reset the indexes which are used for accessing the BDs */
	lp->tx_bd_ci = 0;
	lp->tx_bd_tail = 0;
	lp->rx_bd_ci = 0;

	size = XEMACPS_SEND_BD_CNT * sizeof(struct ring_info);
	lp->tx_skb = kzalloc(size, GFP_KERNEL);
	if (!lp->tx_skb)
		goto err_out;
	size = XEMACPS_RECV_BD_CNT * sizeof(struct ring_info);
	lp->rx_skb = kzalloc(size, GFP_KERNEL);
	if (!lp->rx_skb)
		goto err_out;

	/* Set up RX buffer descriptors. */

	size = XEMACPS_RECV_BD_CNT * sizeof(struct xemacps_bd);
	lp->rx_bd = dma_alloc_coherent(&lp->pdev->dev, size,
			&lp->rx_bd_dma, GFP_KERNEL);
	if (!lp->rx_bd)
		goto err_out;
	dev_dbg(&lp->pdev->dev, "RX ring %d bytes at 0x%x mapped %p\n",
			size, lp->rx_bd_dma, lp->rx_bd);

	for (i = 0; i < XEMACPS_RECV_BD_CNT; i++) {
		cur_p = &lp->rx_bd[i];

		new_skb = netdev_alloc_skb(lp->ndev, XEMACPS_RX_BUF_SIZE);
		if (new_skb == NULL) {
			dev_err(&lp->ndev->dev, "alloc_skb error %d\n", i);
			goto err_out;
		}

		/* Get dma handle of skb->data */
		new_skb_baddr = (u32) dma_map_single(lp->ndev->dev.parent,
							new_skb->data,
							XEMACPS_RX_BUF_SIZE,
							DMA_FROM_DEVICE);

		/* set wrap bit for last BD */
		regval = (new_skb_baddr & XEMACPS_RXBUF_ADD_MASK);
		if (i == XEMACPS_RECV_BD_CNT - 1)
			regval |= XEMACPS_RXBUF_WRAP_MASK;
		cur_p->addr = regval;
		cur_p->ctrl = 0;
		wmb();

		lp->rx_skb[i].skb = new_skb;
		lp->rx_skb[i].mapping = new_skb_baddr;
		lp->rx_skb[i].len = XEMACPS_RX_BUF_SIZE;
	}

	/* Set up TX buffer descriptors. */

	size = XEMACPS_SEND_BD_CNT * sizeof(struct xemacps_bd);
	lp->tx_bd = dma_alloc_coherent(&lp->pdev->dev, size,
			&lp->tx_bd_dma, GFP_KERNEL);
	if (!lp->tx_bd)
		goto err_out;
	dev_dbg(&lp->pdev->dev, "TX ring %d bytes at 0x%x mapped %p\n",
			size, lp->tx_bd_dma, lp->tx_bd);

	for (i = 0; i < XEMACPS_SEND_BD_CNT; i++) {
		cur_p = &lp->tx_bd[i];
		/* set wrap bit for last BD */
		cur_p->addr = 0;
		regval = XEMACPS_TXBUF_USED_MASK;
		if (i == XEMACPS_SEND_BD_CNT - 1)
			regval |= XEMACPS_TXBUF_WRAP_MASK;
		cur_p->ctrl = regval;
	}
	wmb();

	lp->tx_bd_freecnt = XEMACPS_SEND_BD_CNT;

	dev_dbg(&lp->pdev->dev,
		"lp->tx_bd %p lp->tx_bd_dma %p lp->tx_skb %p\n",
		lp->tx_bd, (void *)lp->tx_bd_dma, lp->tx_skb);
	dev_dbg(&lp->pdev->dev,
		"lp->rx_bd %p lp->rx_bd_dma %p lp->rx_skb %p\n",
		lp->rx_bd, (void *)lp->rx_bd_dma, lp->rx_skb);

	return 0;

err_out:
	xemacps_descriptor_free(lp);
	return -ENOMEM;
}

#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
/*
 * Initialize the GEM Time Stamp Unit
 */
static void xemacps_init_tsu(struct net_local *lp)
{

	memset(&lp->cycles, 0, sizeof(lp->cycles));
	lp->cycles.read = xemacps_read_clock;
	lp->cycles.mask = CLOCKSOURCE_MASK(64);
	lp->cycles.mult = 1;
	lp->cycles.shift = 0;

	/* Set registers so that rollover occurs soon to test this. */
	xemacps_write(lp->baseaddr, XEMACPS_1588NS_OFFSET, 0x00000000);
	xemacps_write(lp->baseaddr, XEMACPS_1588S_OFFSET, 0xFF800000);

	/* program the timer increment register with the numer of nanoseconds
	 * per clock tick.
	 *
	 * Note: The value is calculated based on the current operating
	 * frequency 50MHz
	 */
	xemacps_write(lp->baseaddr, XEMACPS_1588INC_OFFSET,
			(NS_PER_SEC/lp->ptpenetclk));

	timecounter_init(&lp->clock, &lp->cycles,
				ktime_to_ns(ktime_get_real()));
	/*
	 * Synchronize our NIC clock against system wall clock.
	 */
	memset(&lp->compare, 0, sizeof(lp->compare));
	lp->compare.source = &lp->clock;
	lp->compare.target = ktime_get_real;
	lp->compare.num_samples = 10;
	timecompare_update(&lp->compare, 0);

	/* Initialize hwstamp config */
	lp->hwtstamp_config.rx_filter = HWTSTAMP_FILTER_NONE;
	lp->hwtstamp_config.tx_type = HWTSTAMP_TX_OFF;

}
#endif /* CONFIG_XILINX_PS_EMAC_HWTSTAMP */

/**
 * xemacps_init_hw - Initialize hardware to known good state
 * @lp: local device instance pointer
 **/
static void xemacps_init_hw(struct net_local *lp)
{
	u32 regval;

	xemacps_reset_hw(lp);
	xemacps_set_hwaddr(lp);

	/* network configuration */
	regval  = 0;
	regval |= XEMACPS_NWCFG_FDEN_MASK;
	regval |= XEMACPS_NWCFG_RXCHKSUMEN_MASK;
	regval |= XEMACPS_NWCFG_PAUSECOPYDI_MASK;
	regval |= XEMACPS_NWCFG_FCSREM_MASK;
	regval |= XEMACPS_NWCFG_PAUSEEN_MASK;
	regval |= XEMACPS_NWCFG_100_MASK;
	regval |= XEMACPS_NWCFG_HDRXEN_MASK;

	regval |= (MDC_DIV_224 << XEMACPS_NWCFG_MDC_SHIFT_MASK);
	if (lp->ndev->flags & IFF_PROMISC)	/* copy all */
		regval |= XEMACPS_NWCFG_COPYALLEN_MASK;
	if (!(lp->ndev->flags & IFF_BROADCAST))	/* No broadcast */
		regval |= XEMACPS_NWCFG_BCASTDI_MASK;
	xemacps_write(lp->baseaddr, XEMACPS_NWCFG_OFFSET, regval);

	/* Init TX and RX DMA Q address */
	xemacps_write(lp->baseaddr, XEMACPS_RXQBASE_OFFSET, lp->rx_bd_dma);
	xemacps_write(lp->baseaddr, XEMACPS_TXQBASE_OFFSET, lp->tx_bd_dma);

	/* DMACR configurations */
	regval  = (((XEMACPS_RX_BUF_SIZE / XEMACPS_RX_BUF_UNIT) +
		((XEMACPS_RX_BUF_SIZE % XEMACPS_RX_BUF_UNIT) ? 1 : 0)) <<
		XEMACPS_DMACR_RXBUF_SHIFT);
	regval |= XEMACPS_DMACR_RXSIZE_MASK;
	regval |= XEMACPS_DMACR_TXSIZE_MASK;
	regval |= XEMACPS_DMACR_TCPCKSUM_MASK;
#ifdef __LITTLE_ENDIAN
	regval &= ~XEMACPS_DMACR_ENDIAN_MASK;
#endif
#ifdef __BIG_ENDIAN
	regval |= XEMACPS_DMACR_ENDIAN_MASK;
#endif
	regval |= XEMACPS_DMACR_BLENGTH_INCR16;
	xemacps_write(lp->baseaddr, XEMACPS_DMACR_OFFSET, regval);

	/* Enable TX, RX and MDIO port */
	regval  = 0;
	regval |= XEMACPS_NWCTRL_MDEN_MASK;
	regval |= XEMACPS_NWCTRL_TXEN_MASK;
	regval |= XEMACPS_NWCTRL_RXEN_MASK;
	xemacps_write(lp->baseaddr, XEMACPS_NWCTRL_OFFSET, regval);

#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
	/* Initialize the Time Stamp Unit */
	xemacps_init_tsu(lp);
#endif

	/* Enable interrupts */
	regval  = XEMACPS_IXR_ALL_MASK;
	xemacps_write(lp->baseaddr, XEMACPS_IER_OFFSET, regval);
}

/**
 * xemacps_resetrx_for_no_rxdata - Resets the Rx if there is no data
 * for a while (presently 100 msecs)
 * @data: Used for net_local instance pointer
 **/
static void xemacps_resetrx_for_no_rxdata(unsigned long data)
{
	struct net_local *lp = (struct net_local *)data;
	unsigned long regctrl;
	unsigned long tempcntr;
	unsigned long flags;

	tempcntr = xemacps_read(lp->baseaddr, XEMACPS_RXCNT_OFFSET);
	if ((!tempcntr) && (!(lp->lastrxfrmscntr))) {
		spin_lock_irqsave(&lp->nwctrlreg_lock, flags);
		regctrl = xemacps_read(lp->baseaddr,
				XEMACPS_NWCTRL_OFFSET);
		regctrl &= (~XEMACPS_NWCTRL_RXEN_MASK);
		xemacps_write(lp->baseaddr,
				XEMACPS_NWCTRL_OFFSET, regctrl);
		regctrl = xemacps_read(lp->baseaddr, XEMACPS_NWCTRL_OFFSET);
		regctrl |= (XEMACPS_NWCTRL_RXEN_MASK);
		xemacps_write(lp->baseaddr, XEMACPS_NWCTRL_OFFSET, regctrl);
		spin_unlock_irqrestore(&lp->nwctrlreg_lock, flags);
	}
	lp->lastrxfrmscntr = tempcntr;
}

/**
 * xemacps_update_stats - Update the statistic structure entries from
 * the corresponding emacps hardware statistic registers
 * @data: Used for net_local instance pointer
 **/
static void xemacps_update_stats(unsigned long data)
{
	struct net_local *lp = (struct net_local *)data;
	struct net_device_stats *nstat = &lp->stats;
	u32 cnt;

	cnt = xemacps_read(lp->baseaddr, XEMACPS_RXUNDRCNT_OFFSET);
	nstat->rx_errors += cnt;
	nstat->rx_length_errors += cnt;

	cnt = xemacps_read(lp->baseaddr, XEMACPS_RXOVRCNT_OFFSET);
	nstat->rx_errors += cnt;
	nstat->rx_length_errors += cnt;

	cnt = xemacps_read(lp->baseaddr, XEMACPS_RXJABCNT_OFFSET);
	nstat->rx_errors += cnt;
	nstat->rx_length_errors += cnt;

	cnt = xemacps_read(lp->baseaddr, XEMACPS_RXFCSCNT_OFFSET);
	nstat->rx_errors += cnt;
	nstat->rx_crc_errors += cnt;

	cnt = xemacps_read(lp->baseaddr, XEMACPS_RXLENGTHCNT_OFFSET);
	nstat->rx_errors += cnt;
	nstat->rx_length_errors += cnt;

	cnt = xemacps_read(lp->baseaddr, XEMACPS_RXALIGNCNT_OFFSET);
	nstat->rx_errors += cnt;
	nstat->rx_frame_errors += cnt;

	cnt = xemacps_read(lp->baseaddr, XEMACPS_RXRESERRCNT_OFFSET);
	nstat->rx_errors += cnt;
	nstat->rx_missed_errors += cnt;

	cnt = xemacps_read(lp->baseaddr, XEMACPS_RXORCNT_OFFSET);
	nstat->rx_errors += cnt;
	nstat->rx_fifo_errors += cnt;

	cnt = xemacps_read(lp->baseaddr, XEMACPS_TXURUNCNT_OFFSET);
	nstat->tx_errors += cnt;
	nstat->tx_fifo_errors += cnt;

	cnt = xemacps_read(lp->baseaddr, XEMACPS_SNGLCOLLCNT_OFFSET);
	nstat->collisions += cnt;

	cnt = xemacps_read(lp->baseaddr, XEMACPS_MULTICOLLCNT_OFFSET);
	nstat->collisions += cnt;

	cnt = xemacps_read(lp->baseaddr, XEMACPS_EXCESSCOLLCNT_OFFSET);
	nstat->tx_errors += cnt;
	nstat->tx_aborted_errors += cnt;
	nstat->collisions += cnt;

	cnt = xemacps_read(lp->baseaddr, XEMACPS_LATECOLLCNT_OFFSET);
	nstat->tx_errors += cnt;
	nstat->collisions += cnt;

	cnt = xemacps_read(lp->baseaddr, XEMACPS_CSENSECNT_OFFSET);
	nstat->tx_errors += cnt;
	nstat->tx_carrier_errors += cnt;
}

/**
 * xemacps_gen_purpose_timerhandler - Timer handler that is called at regular
 * intervals upon expiry of the gen_purpose_timer defined in net_local struct.
 * @data: Used for net_local instance pointer
 *
 * This timer handler is used to update the statistics by calling the API
 * xemacps_update_stats. The statistics register can typically overflow pretty
 * quickly under heavy load conditions. This timer is used to periodically
 * read the stats registers and update the corresponding stats structure
 * entries. The stats registers when read reset to 0.
 **/
static void xemacps_gen_purpose_timerhandler(unsigned long data)
{
	struct net_local *lp = (struct net_local *)data;

	xemacps_update_stats(data);
	xemacps_resetrx_for_no_rxdata(data);
	mod_timer(&(lp->gen_purpose_timer),
		jiffies + msecs_to_jiffies(XEAMCPS_GEN_PURPOSE_TIMER_LOAD));
}

/**
 * xemacps_open - Called when a network device is made active
 * @ndev: network interface device structure
 * return 0 on success, negative value if error
 *
 * The open entry point is called when a network interface is made active
 * by the system (IFF_UP). At this point all resources needed for transmit
 * and receive operations are allocated, the interrupt handler is
 * registered with OS, the watchdog timer is started, and the stack is
 * notified that the interface is ready.
 *
 * note: if error(s), allocated resources before error require to be
 * released or system issues (such as memory) leak might happen.
 **/
static int xemacps_open(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	int rc;

	dev_dbg(&lp->pdev->dev, "open\n");
	if (!is_valid_ether_addr(ndev->dev_addr))
		return  -EADDRNOTAVAIL;

	rc = xemacps_descriptor_init(lp);
	if (rc) {
		dev_err(&lp->pdev->dev,
			"Unable to allocate DMA memory, rc %d\n", rc);
		return rc;
	}

	rc = pm_runtime_get_sync(&lp->pdev->dev);
	if (rc < 0) {
		dev_err(&lp->pdev->dev,
			"pm_runtime_get_sync() failed, rc %d\n", rc);
		goto err_free_rings;
	}

	xemacps_init_hw(lp);
	rc = xemacps_mii_probe(ndev);
	if (rc != 0) {
		dev_err(&lp->pdev->dev,
			"%s mii_probe fail.\n", lp->mii_bus->name);
		if (rc == (-2)) {
			mdiobus_unregister(lp->mii_bus);
			kfree(lp->mii_bus->irq);
			mdiobus_free(lp->mii_bus);
		}
		rc = -ENXIO;
		goto err_pm_put;
	}

	setup_timer(&(lp->gen_purpose_timer), xemacps_gen_purpose_timerhandler,
							(unsigned long)lp);
	mod_timer(&(lp->gen_purpose_timer),
		jiffies + msecs_to_jiffies(XEAMCPS_GEN_PURPOSE_TIMER_LOAD));

	napi_enable(&lp->napi);
	netif_carrier_on(ndev);
	netif_start_queue(ndev);
	tasklet_enable(&lp->tx_bdreclaim_tasklet);

	return 0;

err_pm_put:
	xemacps_reset_hw(lp);
	pm_runtime_put(&lp->pdev->dev);
err_free_rings:
	xemacps_descriptor_free(lp);

	return rc;
}

/**
 * xemacps_close - disable a network interface
 * @ndev: network interface device structure
 * return 0
 *
 * The close entry point is called when a network interface is de-activated
 * by OS. The hardware is still under the driver control, but needs to be
 * disabled. A global MAC reset is issued to stop the hardware, and all
 * transmit and receive resources are freed.
 **/
static int xemacps_close(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);

	del_timer_sync(&(lp->gen_purpose_timer));
	netif_stop_queue(ndev);
	napi_disable(&lp->napi);
	tasklet_disable(&lp->tx_bdreclaim_tasklet);
	netif_carrier_off(ndev);
	if (lp->phy_dev)
		phy_disconnect(lp->phy_dev);
	if (lp->gmii2rgmii_phy_node)
		phy_disconnect(lp->gmii2rgmii_phy_dev);
	xemacps_reset_hw(lp);
	mdelay(500);
	xemacps_descriptor_free(lp);

	pm_runtime_put(&lp->pdev->dev);

	return 0;
}

/**
 * xemacps_reinit_for_txtimeout - work queue scheduled for the tx timeout
 * handling.
 * @ndev: queue work structure
 **/
static void xemacps_reinit_for_txtimeout(struct work_struct *data)
{
	struct net_local *lp = container_of(data, struct net_local,
		txtimeout_reinit);
	int rc;

	netif_stop_queue(lp->ndev);
	napi_disable(&lp->napi);
	tasklet_disable(&lp->tx_bdreclaim_tasklet);
	spin_lock_bh(&lp->tx_lock);
	xemacps_reset_hw(lp);
	spin_unlock_bh(&lp->tx_lock);

	if (lp->phy_dev)
		phy_stop(lp->phy_dev);

	xemacps_descriptor_free(lp);
	rc = xemacps_descriptor_init(lp);
	if (rc) {
		dev_err(&lp->pdev->dev,
			"Unable to allocate DMA memory, rc %d\n", rc);
		return;
	}

	xemacps_init_hw(lp);

	lp->link    = 0;
	lp->speed   = 0;
	lp->duplex  = -1;

	if (lp->phy_dev)
		phy_start(lp->phy_dev);

	napi_enable(&lp->napi);
	tasklet_enable(&lp->tx_bdreclaim_tasklet);
	lp->ndev->trans_start = jiffies;
	netif_wake_queue(lp->ndev);
}

/**
 * xemacps_tx_timeout - callback used when the transmitter has not made
 * any progress for dev->watchdog ticks.
 * @ndev: network interface device structure
 **/
static void xemacps_tx_timeout(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);

	dev_err(&lp->pdev->dev, "transmit timeout %lu ms, reseting...\n",
		TX_TIMEOUT * 1000UL / HZ);
	queue_work(lp->txtimeout_handler_wq, &lp->txtimeout_reinit);
}

/**
 * xemacps_set_mac_address - set network interface mac address
 * @ndev: network interface device structure
 * @addr: pointer to MAC address
 * return 0 on success, negative value if error
 **/
static int xemacps_set_mac_address(struct net_device *ndev, void *addr)
{
	struct net_local *lp = netdev_priv(ndev);
	struct sockaddr *hwaddr = (struct sockaddr *)addr;

	if (netif_running(ndev))
		return -EBUSY;

	if (!is_valid_ether_addr(hwaddr->sa_data))
		return -EADDRNOTAVAIL;

	dev_dbg(&lp->pdev->dev, "hwaddr 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		hwaddr->sa_data[0], hwaddr->sa_data[1], hwaddr->sa_data[2],
		hwaddr->sa_data[3], hwaddr->sa_data[4], hwaddr->sa_data[5]);

	memcpy(ndev->dev_addr, hwaddr->sa_data, ndev->addr_len);

	xemacps_set_hwaddr(lp);
	return 0;
}

/**
 * xemacps_clear_csum - Clear the csum field for  transport protocols
 * @skb: socket buffer
 * @ndev: network interface device structure
 * return 0 on success, other value if error
 **/
static int xemacps_clear_csum(struct sk_buff *skb, struct net_device *ndev)
{
	/* Only run for packets requiring a checksum. */
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	if (unlikely(skb_cow_head(skb, 0)))
		return -1;

	*(__sum16 *)(skb->head + skb->csum_start + skb->csum_offset) = 0;

	return 0;
}

/**
 * xemacps_start_xmit - transmit a packet (called by kernel)
 * @skb: socket buffer
 * @ndev: network interface device structure
 * return 0 on success, other value if error
 **/
static int xemacps_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	dma_addr_t  mapping;
	unsigned int nr_frags, len;
	int i;
	u32 regval;
	void       *virt_addr;
	skb_frag_t *frag;
	struct xemacps_bd *cur_p;
	unsigned long flags;
	u32 bd_tail;

	nr_frags = skb_shinfo(skb)->nr_frags + 1;
	spin_lock_bh(&lp->tx_lock);

	if (nr_frags > lp->tx_bd_freecnt) {
		netif_stop_queue(ndev); /* stop send queue */
		spin_unlock_bh(&lp->tx_lock);
		return NETDEV_TX_BUSY;
	}

	if (xemacps_clear_csum(skb, ndev)) {
		spin_unlock_bh(&lp->tx_lock);
		kfree(skb);
		return NETDEV_TX_OK;
	}

	bd_tail = lp->tx_bd_tail;
	cur_p = &lp->tx_bd[bd_tail];
	lp->tx_bd_freecnt -= nr_frags;
	frag = &skb_shinfo(skb)->frags[0];

	for (i = 0; i < nr_frags; i++) {
		if (i == 0) {
			len = skb_headlen(skb);
			mapping = dma_map_single(&lp->pdev->dev, skb->data,
				len, DMA_TO_DEVICE);
		} else {
			len = skb_frag_size(frag);
			virt_addr = skb_frag_address(frag);
			mapping = dma_map_single(&lp->pdev->dev, virt_addr,
				len, DMA_TO_DEVICE);
			frag++;
			skb_get(skb);
		}

		lp->tx_skb[lp->tx_bd_tail].skb = skb;
		lp->tx_skb[lp->tx_bd_tail].mapping = mapping;
		lp->tx_skb[lp->tx_bd_tail].len = len;
		cur_p->addr = mapping;

		/* preserve critical status bits */
		regval = cur_p->ctrl;
		regval &= (XEMACPS_TXBUF_USED_MASK | XEMACPS_TXBUF_WRAP_MASK);
		/* update length field */
		regval |= ((regval & ~XEMACPS_TXBUF_LEN_MASK) | len);
		/* commit second to last buffer to hardware */
		if (i != 0)
			regval &= ~XEMACPS_TXBUF_USED_MASK;
		/* last fragment of this packet? */
		if (i == (nr_frags - 1))
			regval |= XEMACPS_TXBUF_LAST_MASK;
		cur_p->ctrl = regval;

		lp->tx_bd_tail++;
		lp->tx_bd_tail = lp->tx_bd_tail % XEMACPS_SEND_BD_CNT;
		cur_p = &(lp->tx_bd[lp->tx_bd_tail]);
	}
	wmb();

	/* commit first buffer to hardware -- do this after
	 * committing the other buffers to avoid an underrun */
	cur_p = &lp->tx_bd[bd_tail];
	regval = cur_p->ctrl;
	regval &= ~XEMACPS_TXBUF_USED_MASK;
	cur_p->ctrl = regval;
	wmb();

	spin_lock_irqsave(&lp->nwctrlreg_lock, flags);
	regval = xemacps_read(lp->baseaddr, XEMACPS_NWCTRL_OFFSET);
	xemacps_write(lp->baseaddr, XEMACPS_NWCTRL_OFFSET,
			(regval | XEMACPS_NWCTRL_STARTTX_MASK));
	spin_unlock_irqrestore(&lp->nwctrlreg_lock, flags);

	spin_unlock_bh(&lp->tx_lock);
	ndev->trans_start = jiffies;
	return 0;
}

/* Get the MAC Address bit from the specified position */
static unsigned get_bit(u8 *mac, unsigned bit)
{
	unsigned byte;

	byte = mac[bit / 8];
	byte >>= (bit & 0x7);
	byte &= 1;

	return byte;
}

/* Calculate a GEM MAC Address hash index */
static unsigned calc_mac_hash(u8 *mac)
{
	int index_bit, mac_bit;
	unsigned hash_index;

	hash_index = 0;
	mac_bit = 5;
	for (index_bit = 5; index_bit >= 0; index_bit--) {
		hash_index |= (get_bit(mac,  mac_bit) ^
					get_bit(mac, mac_bit + 6) ^
					get_bit(mac, mac_bit + 12) ^
					get_bit(mac, mac_bit + 18) ^
					get_bit(mac, mac_bit + 24) ^
					get_bit(mac, mac_bit + 30) ^
					get_bit(mac, mac_bit + 36) ^
					get_bit(mac, mac_bit + 42))
						<< index_bit;
		mac_bit--;
	}

	return hash_index;
}

/**
 * xemacps_set_hashtable - Add multicast addresses to the internal
 * multicast-hash table. Called from xemac_set_rx_mode().
 * @ndev: network interface device structure
 *
 * The hash address register is 64 bits long and takes up two
 * locations in the memory map.  The least significant bits are stored
 * in EMAC_HSL and the most significant bits in EMAC_HSH.
 *
 * The unicast hash enable and the multicast hash enable bits in the
 * network configuration register enable the reception of hash matched
 * frames. The destination address is reduced to a 6 bit index into
 * the 64 bit hash register using the following hash function.  The
 * hash function is an exclusive or of every sixth bit of the
 * destination address.
 *
 * hi[5] = da[5] ^ da[11] ^ da[17] ^ da[23] ^ da[29] ^ da[35] ^ da[41] ^ da[47]
 * hi[4] = da[4] ^ da[10] ^ da[16] ^ da[22] ^ da[28] ^ da[34] ^ da[40] ^ da[46]
 * hi[3] = da[3] ^ da[09] ^ da[15] ^ da[21] ^ da[27] ^ da[33] ^ da[39] ^ da[45]
 * hi[2] = da[2] ^ da[08] ^ da[14] ^ da[20] ^ da[26] ^ da[32] ^ da[38] ^ da[44]
 * hi[1] = da[1] ^ da[07] ^ da[13] ^ da[19] ^ da[25] ^ da[31] ^ da[37] ^ da[43]
 * hi[0] = da[0] ^ da[06] ^ da[12] ^ da[18] ^ da[24] ^ da[30] ^ da[36] ^ da[42]
 *
 * da[0] represents the least significant bit of the first byte
 * received, that is, the multicast/unicast indicator, and da[47]
 * represents the most significant bit of the last byte received.  If
 * the hash index, hi[n], points to a bit that is set in the hash
 * register then the frame will be matched according to whether the
 * frame is multicast or unicast.  A multicast match will be signalled
 * if the multicast hash enable bit is set, da[0] is 1 and the hash
 * index points to a bit set in the hash register.  A unicast match
 * will be signalled if the unicast hash enable bit is set, da[0] is 0
 * and the hash index points to a bit set in the hash register.  To
 * receive all multicast frames, the hash register should be set with
 * all ones and the multicast hash enable bit should be set in the
 * network configuration register.
 **/
static void xemacps_set_hashtable(struct net_device *ndev)
{
	struct netdev_hw_addr *curr;
	u32 regvalh, regvall, hash_index;
	u8 *mc_addr;
	struct net_local *lp;

	lp = netdev_priv(ndev);

	regvalh = regvall = 0;

	netdev_for_each_mc_addr(curr, ndev) {
		if (!curr)	/* end of list */
			break;
		mc_addr = curr->addr;
		hash_index = calc_mac_hash(mc_addr);

		if (hash_index >= XEMACPS_MAX_HASH_BITS) {
			dev_err(&lp->pdev->dev,
					"hash calculation out of range %d\n",
					hash_index);
			break;
		}
		if (hash_index < 32)
			regvall |= (1 << hash_index);
		else
			regvalh |= (1 << (hash_index - 32));
	}

	xemacps_write(lp->baseaddr, XEMACPS_HASHL_OFFSET, regvall);
	xemacps_write(lp->baseaddr, XEMACPS_HASHH_OFFSET, regvalh);
}

/**
 * xemacps_set_rx_mode - enable/disable promiscuous and multicast modes
 * @ndev: network interface device structure
 **/
static void xemacps_set_rx_mode(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	u32 regval;

	regval = xemacps_read(lp->baseaddr, XEMACPS_NWCFG_OFFSET);

	/* promisc mode */
	if (ndev->flags & IFF_PROMISC)
		regval |= XEMACPS_NWCFG_COPYALLEN_MASK;
	if (!(ndev->flags & IFF_PROMISC))
		regval &= ~XEMACPS_NWCFG_COPYALLEN_MASK;

	/* All multicast mode */
	if (ndev->flags & IFF_ALLMULTI) {
		regval |= XEMACPS_NWCFG_MCASTHASHEN_MASK;
		xemacps_write(lp->baseaddr, XEMACPS_HASHL_OFFSET, ~0UL);
		xemacps_write(lp->baseaddr, XEMACPS_HASHH_OFFSET, ~0UL);
	/* Specific multicast mode */
	} else if ((ndev->flags & IFF_MULTICAST)
			&& (netdev_mc_count(ndev) > 0)) {
		regval |= XEMACPS_NWCFG_MCASTHASHEN_MASK;
		xemacps_set_hashtable(ndev);
	/* Disable multicast mode */
	} else {
		xemacps_write(lp->baseaddr, XEMACPS_HASHL_OFFSET, 0x0);
		xemacps_write(lp->baseaddr, XEMACPS_HASHH_OFFSET, 0x0);
		regval &= ~XEMACPS_NWCFG_MCASTHASHEN_MASK;
	}

	/* broadcast mode */
	if (ndev->flags & IFF_BROADCAST)
		regval &= ~XEMACPS_NWCFG_BCASTDI_MASK;
	/* No broadcast */
	if (!(ndev->flags & IFF_BROADCAST))
		regval |= XEMACPS_NWCFG_BCASTDI_MASK;

	xemacps_write(lp->baseaddr, XEMACPS_NWCFG_OFFSET, regval);
}

#define MIN_MTU 60
#define MAX_MTU 1500
/**
 * xemacps_change_mtu - Change maximum transfer unit
 * @ndev: network interface device structure
 * @new_mtu: new vlaue for maximum frame size
 * return: 0 on success, negative value if error.
 **/
static int xemacps_change_mtu(struct net_device *ndev, int new_mtu)
{
	if ((new_mtu < MIN_MTU) ||
		((new_mtu + ndev->hard_header_len) > MAX_MTU))
		return -EINVAL;

	ndev->mtu = new_mtu;	/* change mtu in net_device structure */
	return 0;
}

/**
 * xemacps_get_settings - get device specific settings.
 * Usage: Issue "ethtool ethX" under linux prompt.
 * @ndev: network device
 * @ecmd: ethtool command structure
 * return: 0 on success, negative value if error.
 **/
static int
xemacps_get_settings(struct net_device *ndev, struct ethtool_cmd *ecmd)
{
	struct net_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = lp->phy_dev;

	if (!phydev)
		return -ENODEV;

	return phy_ethtool_gset(phydev, ecmd);
}

/**
 * xemacps_set_settings - set device specific settings.
 * Usage: Issue "ethtool -s ethX speed 1000" under linux prompt
 * to change speed
 * @ndev: network device
 * @ecmd: ethtool command structure
 * return: 0 on success, negative value if error.
 **/
static int
xemacps_set_settings(struct net_device *ndev, struct ethtool_cmd *ecmd)
{
	struct net_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = lp->phy_dev;

	if (!phydev)
		return -ENODEV;

	return phy_ethtool_sset(phydev, ecmd);
}

/**
 * xemacps_get_drvinfo - report driver information
 * Usage: Issue "ethtool -i ethX" under linux prompt
 * @ndev: network device
 * @ed: device driver information structure
 **/
static void
xemacps_get_drvinfo(struct net_device *ndev, struct ethtool_drvinfo *ed)
{
	struct net_local *lp = netdev_priv(ndev);

	memset(ed, 0, sizeof(struct ethtool_drvinfo));
	strcpy(ed->driver, lp->pdev->dev.driver->name);
	strcpy(ed->version, DRIVER_VERSION);
}

/**
 * xemacps_get_ringparam - get device dma ring information.
 * Usage: Issue "ethtool -g ethX" under linux prompt
 * @ndev: network device
 * @erp: ethtool ring parameter structure
 **/
static void
xemacps_get_ringparam(struct net_device *ndev, struct ethtool_ringparam *erp)
{
	memset(erp, 0, sizeof(struct ethtool_ringparam));

	erp->rx_max_pending = XEMACPS_RECV_BD_CNT;
	erp->tx_max_pending = XEMACPS_SEND_BD_CNT;
	erp->rx_pending = 0;
	erp->tx_pending = 0;
}

/**
 * xemacps_get_wol - get device wake on lan status
 * Usage: Issue "ethtool ethX" under linux prompt
 * @ndev: network device
 * @ewol: wol status
 **/
static void
xemacps_get_wol(struct net_device *ndev, struct ethtool_wolinfo *ewol)
{
	struct net_local *lp = netdev_priv(ndev);
	u32 regval;

	ewol->supported = WAKE_MAGIC | WAKE_ARP | WAKE_UCAST | WAKE_MCAST;

	regval = xemacps_read(lp->baseaddr, XEMACPS_WOL_OFFSET);
	if (regval | XEMACPS_WOL_MCAST_MASK)
		ewol->wolopts |= WAKE_MCAST;
	if (regval | XEMACPS_WOL_ARP_MASK)
		ewol->wolopts |= WAKE_ARP;
	if (regval | XEMACPS_WOL_SPEREG1_MASK)
		ewol->wolopts |= WAKE_UCAST;
	if (regval | XEMACPS_WOL_MAGIC_MASK)
		ewol->wolopts |= WAKE_MAGIC;

}

/**
 * xemacps_set_wol - set device wake on lan configuration
 * Usage: Issue "ethtool -s ethX wol u|m|b|g" under linux prompt to enable
 * specified type of packet.
 * Usage: Issue "ethtool -s ethX wol d" under linux prompt to disable
 * this feature.
 * @ndev: network device
 * @ewol: wol status
 * return 0 on success, negative value if not supported
 **/
static int
xemacps_set_wol(struct net_device *ndev, struct ethtool_wolinfo *ewol)
{
	struct net_local *lp = netdev_priv(ndev);
	u32 regval;

	if (ewol->wolopts & ~(WAKE_MAGIC | WAKE_ARP | WAKE_UCAST | WAKE_MCAST))
		return -EOPNOTSUPP;

	regval  = xemacps_read(lp->baseaddr, XEMACPS_WOL_OFFSET);
	regval &= ~(XEMACPS_WOL_MCAST_MASK | XEMACPS_WOL_ARP_MASK |
		XEMACPS_WOL_SPEREG1_MASK | XEMACPS_WOL_MAGIC_MASK);

	if (ewol->wolopts & WAKE_MAGIC)
		regval |= XEMACPS_WOL_MAGIC_MASK;
	if (ewol->wolopts & WAKE_ARP)
		regval |= XEMACPS_WOL_ARP_MASK;
	if (ewol->wolopts & WAKE_UCAST)
		regval |= XEMACPS_WOL_SPEREG1_MASK;
	if (ewol->wolopts & WAKE_MCAST)
		regval |= XEMACPS_WOL_MCAST_MASK;

	xemacps_write(lp->baseaddr, XEMACPS_WOL_OFFSET, regval);

	return 0;
}

/**
 * xemacps_get_pauseparam - get device pause status
 * Usage: Issue "ethtool -a ethX" under linux prompt
 * @ndev: network device
 * @epauseparam: pause parameter
 *
 * note: hardware supports only tx flow control
 **/
static void
xemacps_get_pauseparam(struct net_device *ndev,
		struct ethtool_pauseparam *epauseparm)
{
	struct net_local *lp = netdev_priv(ndev);
	u32 regval;

	epauseparm->autoneg  = 0;
	epauseparm->rx_pause = 0;

	regval = xemacps_read(lp->baseaddr, XEMACPS_NWCFG_OFFSET);
	epauseparm->tx_pause = regval & XEMACPS_NWCFG_PAUSEEN_MASK;
}

/**
 * xemacps_set_pauseparam - set device pause parameter(flow control)
 * Usage: Issue "ethtool -A ethX tx on|off" under linux prompt
 * @ndev: network device
 * @epauseparam: pause parameter
 * return 0 on success, negative value if not supported
 *
 * note: hardware supports only tx flow control
 **/
static int
xemacps_set_pauseparam(struct net_device *ndev,
		struct ethtool_pauseparam *epauseparm)
{
	struct net_local *lp = netdev_priv(ndev);
	u32 regval;

	if (netif_running(ndev)) {
		dev_err(&lp->pdev->dev,
			"Please stop netif before apply configruation\n");
		return -EFAULT;
	}

	regval = xemacps_read(lp->baseaddr, XEMACPS_NWCFG_OFFSET);

	if (epauseparm->tx_pause)
		regval |= XEMACPS_NWCFG_PAUSEEN_MASK;
	if (!(epauseparm->tx_pause))
		regval &= ~XEMACPS_NWCFG_PAUSEEN_MASK;

	xemacps_write(lp->baseaddr, XEMACPS_NWCFG_OFFSET, regval);
	return 0;
}

/**
 * xemacps_get_stats - get device statistic raw data in 64bit mode
 * @ndev: network device
 **/
static struct net_device_stats
*xemacps_get_stats(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	struct net_device_stats *nstat = &lp->stats;

	xemacps_update_stats((unsigned long)lp);
	return nstat;
}

static struct ethtool_ops xemacps_ethtool_ops = {
	.get_settings   = xemacps_get_settings,
	.set_settings   = xemacps_set_settings,
	.get_drvinfo    = xemacps_get_drvinfo,
	.get_link       = ethtool_op_get_link, /* ethtool default */
	.get_ringparam  = xemacps_get_ringparam,
	.get_wol        = xemacps_get_wol,
	.set_wol        = xemacps_set_wol,
	.get_pauseparam = xemacps_get_pauseparam,
	.set_pauseparam = xemacps_set_pauseparam,
};

#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
static int xemacps_hwtstamp_ioctl(struct net_device *netdev,
				struct ifreq *ifr, int cmd)
{
	struct hwtstamp_config config;
	struct net_local *lp;
	u32 regval;

	lp = netdev_priv(netdev);

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	/* reserved for future extensions */
	if (config.flags)
		return -EINVAL;

	if ((config.tx_type != HWTSTAMP_TX_OFF) &&
		(config.tx_type != HWTSTAMP_TX_ON))
		return -ERANGE;

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		regval = xemacps_read(lp->baseaddr, XEMACPS_NWCTRL_OFFSET);
		xemacps_write(lp->baseaddr, XEMACPS_NWCTRL_OFFSET,
			(regval | XEMACPS_NWCTRL_RXTSTAMP_MASK));
		break;
	default:
		return -ERANGE;
	}

	config.tx_type = HWTSTAMP_TX_ON;
	lp->hwtstamp_config = config;

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}
#endif /* CONFIG_XILINX_PS_EMAC_HWTSTAMP */

/**
 * xemacps_ioctl - ioctl entry point
 * @ndev: network device
 * @rq: interface request ioctl
 * @cmd: command code
 *
 * Called when user issues an ioctl request to the network device.
 **/
static int xemacps_ioctl(struct net_device *ndev, struct ifreq *rq, int cmd)
{
	struct net_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = lp->phy_dev;

	if (!netif_running(ndev))
		return -EINVAL;

	if (!phydev)
		return -ENODEV;

	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		return phy_mii_ioctl(phydev, rq, cmd);
#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
	case SIOCSHWTSTAMP:
		return xemacps_hwtstamp_ioctl(ndev, rq, cmd);
#endif
	default:
		dev_info(&lp->pdev->dev, "ioctl %d not implemented.\n", cmd);
		return -EOPNOTSUPP;
	}

}

/**
 * xemacps_probe - Platform driver probe
 * @pdev: Pointer to platform device structure
 *
 * Return 0 on success, negative value if error
 */
static int xemacps_probe(struct platform_device *pdev)
{
	struct resource *r_mem = NULL;
	struct resource *r_irq = NULL;
	struct net_device *ndev;
	struct net_local *lp;
	u32 regval = 0;
	int rc = -ENXIO;

	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	r_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!r_mem || !r_irq) {
		dev_err(&pdev->dev, "no IO resource defined.\n");
		return -ENXIO;
	}

	ndev = alloc_etherdev(sizeof(*lp));
	if (!ndev) {
		dev_err(&pdev->dev, "etherdev allocation failed.\n");
		return -ENOMEM;
	}

	SET_NETDEV_DEV(ndev, &pdev->dev);

	lp = netdev_priv(ndev);
	lp->pdev = pdev;
	lp->ndev = ndev;

	spin_lock_init(&lp->tx_lock);
	spin_lock_init(&lp->rx_lock);
	spin_lock_init(&lp->nwctrlreg_lock);

	lp->baseaddr = devm_ioremap_resource(&pdev->dev, r_mem);
	if (IS_ERR(lp->baseaddr)) {
		dev_err(&pdev->dev, "failed to map baseaddress.\n");
		rc = PTR_ERR(lp->baseaddr);
		goto err_out_free_netdev;
	}

	dev_dbg(&lp->pdev->dev, "BASEADDRESS hw: %p virt: %p\n",
			(void *)r_mem->start, lp->baseaddr);

	ndev->irq = platform_get_irq(pdev, 0);

	ndev->netdev_ops = &netdev_ops;
	ndev->watchdog_timeo = TX_TIMEOUT;
	ndev->ethtool_ops = &xemacps_ethtool_ops;
	ndev->base_addr = r_mem->start;
	ndev->features = NETIF_F_IP_CSUM | NETIF_F_SG;
	netif_napi_add(ndev, &lp->napi, xemacps_rx_poll, XEMACPS_NAPI_WEIGHT);

	lp->ip_summed = CHECKSUM_UNNECESSARY;

	rc = register_netdev(ndev);
	if (rc) {
		dev_err(&pdev->dev, "Cannot register net device, aborting.\n");
		goto err_out_free_netdev;
	}

	if (ndev->irq == 54)
		lp->enetnum = 0;
	else
		lp->enetnum = 1;

	lp->aperclk = devm_clk_get(&pdev->dev, "aper_clk");
	if (IS_ERR(lp->aperclk)) {
		dev_err(&pdev->dev, "aper_clk clock not found.\n");
		rc = PTR_ERR(lp->aperclk);
		goto err_out_unregister_netdev;
	}
	lp->devclk = devm_clk_get(&pdev->dev, "ref_clk");
	if (IS_ERR(lp->devclk)) {
		dev_err(&pdev->dev, "ref_clk clock not found.\n");
		rc = PTR_ERR(lp->devclk);
		goto err_out_unregister_netdev;
	}

	rc = clk_prepare_enable(lp->aperclk);
	if (rc) {
		dev_err(&pdev->dev, "Unable to enable APER clock.\n");
		goto err_out_unregister_netdev;
	}
	rc = clk_prepare_enable(lp->devclk);
	if (rc) {
		dev_err(&pdev->dev, "Unable to enable device clock.\n");
		goto err_out_clk_dis_aper;
	}

	lp->clk_rate_change_nb.notifier_call = xemacps_clk_notifier_cb;
	lp->clk_rate_change_nb.next = NULL;
	if (clk_notifier_register(lp->devclk, &lp->clk_rate_change_nb))
		dev_warn(&pdev->dev,
			"Unable to register clock notifier.\n");

#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
	prop = of_get_property(lp->pdev->dev.of_node,
				"xlnx,ptp-enet-clock", NULL);
	if (prop)
		lp->ptpenetclk = (u32)be32_to_cpup(prop);
	else
		lp->ptpenetclk = 133333328;
#endif

	lp->phy_node = of_parse_phandle(lp->pdev->dev.of_node,
						"phy-handle", 0);
	lp->gmii2rgmii_phy_node = of_parse_phandle(lp->pdev->dev.of_node,
						"gmii2rgmii-phy-handle", 0);
	rc = of_get_phy_mode(lp->pdev->dev.of_node);
	if (rc < 0) {
		dev_err(&lp->pdev->dev, "error in getting phy i/f\n");
		goto err_out_unregister_clk_notifier;
	}

	lp->phy_interface = rc;

	/* Set MDIO clock divider */
	regval = (MDC_DIV_224 << XEMACPS_NWCFG_MDC_SHIFT_MASK);
	xemacps_write(lp->baseaddr, XEMACPS_NWCFG_OFFSET, regval);


	regval = XEMACPS_NWCTRL_MDEN_MASK;
	xemacps_write(lp->baseaddr, XEMACPS_NWCTRL_OFFSET, regval);

	rc = xemacps_mii_init(lp);
	if (rc) {
		dev_err(&lp->pdev->dev, "error in xemacps_mii_init\n");
		goto err_out_unregister_clk_notifier;
	}

	xemacps_update_hwaddr(lp);
	tasklet_init(&lp->tx_bdreclaim_tasklet, xemacps_tx_poll,
		     (unsigned long) ndev);
	tasklet_disable(&lp->tx_bdreclaim_tasklet);

	lp->txtimeout_handler_wq = create_singlethread_workqueue(DRIVER_NAME);
	INIT_WORK(&lp->txtimeout_reinit, xemacps_reinit_for_txtimeout);

	platform_set_drvdata(pdev, ndev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	dev_info(&lp->pdev->dev, "pdev->id %d, baseaddr 0x%08lx, irq %d\n",
		pdev->id, ndev->base_addr, ndev->irq);

	rc = devm_request_irq(&pdev->dev, ndev->irq, &xemacps_interrupt, 0,
		ndev->name, ndev);
	if (rc) {
		dev_err(&lp->pdev->dev, "Unable to request IRQ %p, error %d\n",
				r_irq, rc);
		goto err_out_unregister_clk_notifier;
	}

	return 0;

err_out_unregister_clk_notifier:
	clk_notifier_unregister(lp->devclk, &lp->clk_rate_change_nb);
	clk_disable_unprepare(lp->devclk);
err_out_clk_dis_aper:
	clk_disable_unprepare(lp->aperclk);
err_out_unregister_netdev:
	unregister_netdev(ndev);
err_out_free_netdev:
	free_netdev(ndev);
	platform_set_drvdata(pdev, NULL);
	return rc;
}

/**
 * xemacps_remove - called when platform driver is unregistered
 * @pdev: Pointer to the platform device structure
 *
 * return: 0 on success
 */
static int xemacps_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct net_local *lp;

	if (ndev) {
		lp = netdev_priv(ndev);

		mdiobus_unregister(lp->mii_bus);
		kfree(lp->mii_bus->irq);
		mdiobus_free(lp->mii_bus);
		unregister_netdev(ndev);

		clk_notifier_unregister(lp->devclk, &lp->clk_rate_change_nb);
		if (!pm_runtime_suspended(&pdev->dev)) {
			clk_disable_unprepare(lp->devclk);
			clk_disable_unprepare(lp->aperclk);
		} else {
			clk_unprepare(lp->devclk);
			clk_unprepare(lp->aperclk);
		}

		free_netdev(ndev);
	}

	return 0;
}

#ifdef CONFIG_PM
#ifdef CONFIG_PM_SLEEP
/**
 * xemacps_suspend - Suspend event
 * @device: Pointer to device structure
 *
 * Return 0
 */
static int xemacps_suspend(struct device *device)
{
	struct platform_device *pdev = container_of(device,
			struct platform_device, dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct net_local *lp = netdev_priv(ndev);

	netif_device_detach(ndev);
	if (!pm_runtime_suspended(device)) {
		clk_disable(lp->devclk);
		clk_disable(lp->aperclk);
	}
	return 0;
}

/**
 * xemacps_resume - Resume after previous suspend
 * @pdev: Pointer to platform device structure
 *
 * Returns 0 on success, errno otherwise.
 */
static int xemacps_resume(struct device *device)
{
	struct platform_device *pdev = container_of(device,
			struct platform_device, dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct net_local *lp = netdev_priv(ndev);

	if (!pm_runtime_suspended(device)) {
		int ret;

		ret = clk_enable(lp->aperclk);
		if (ret)
			return ret;

		ret = clk_enable(lp->devclk);
		if (ret) {
			clk_disable(lp->aperclk);
			return ret;
		}
	}
	netif_device_attach(ndev);
	return 0;
}
#endif /* ! CONFIG_PM_SLEEP */

#ifdef CONFIG_PM_RUNTIME
static int xemacps_runtime_idle(struct device *dev)
{
	return pm_schedule_suspend(dev, 1);
}

static int xemacps_runtime_resume(struct device *device)
{
	int ret;
	struct platform_device *pdev = container_of(device,
			struct platform_device, dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct net_local *lp = netdev_priv(ndev);

	ret = clk_enable(lp->aperclk);
	if (ret)
		return ret;

	ret = clk_enable(lp->devclk);
	if (ret) {
		clk_disable(lp->aperclk);
		return ret;
	}

	return 0;
}

static int xemacps_runtime_suspend(struct device *device)
{
	struct platform_device *pdev = container_of(device,
			struct platform_device, dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct net_local *lp = netdev_priv(ndev);

	clk_disable(lp->devclk);
	clk_disable(lp->aperclk);
	return 0;
}
#endif /* CONFIG_PM_RUNTIME */

static const struct dev_pm_ops xemacps_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xemacps_suspend, xemacps_resume)
	SET_RUNTIME_PM_OPS(xemacps_runtime_suspend, xemacps_runtime_resume,
			xemacps_runtime_idle)
};
#define XEMACPS_PM	(&xemacps_dev_pm_ops)
#else /* ! CONFIG_PM */
#define XEMACPS_PM	NULL
#endif /* ! CONFIG_PM */

static struct net_device_ops netdev_ops = {
	.ndo_open		= xemacps_open,
	.ndo_stop		= xemacps_close,
	.ndo_start_xmit		= xemacps_start_xmit,
	.ndo_set_rx_mode	= xemacps_set_rx_mode,
	.ndo_set_mac_address    = xemacps_set_mac_address,
	.ndo_do_ioctl		= xemacps_ioctl,
	.ndo_change_mtu		= xemacps_change_mtu,
	.ndo_tx_timeout		= xemacps_tx_timeout,
	.ndo_get_stats		= xemacps_get_stats,
};

static struct of_device_id xemacps_of_match[] = {
	{ .compatible = "xlnx,ps7-ethernet-1.00.a", },
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, xemacps_of_match);

static struct platform_driver xemacps_driver = {
	.probe   = xemacps_probe,
	.remove  = xemacps_remove,
	.driver  = {
		.name  = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = xemacps_of_match,
		.pm = XEMACPS_PM,
	},
};

module_platform_driver(xemacps_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx Ethernet driver");
MODULE_LICENSE("GPL v2");
