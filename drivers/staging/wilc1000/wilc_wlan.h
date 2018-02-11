/* SPDX-License-Identifier: GPL-2.0 */
#ifndef WILC_WLAN_H
#define WILC_WLAN_H

#include <linux/types.h>

#define ISWILC1000(id)			((id & 0xfffff000) == 0x100000 ? 1 : 0)

/********************************************
 *
 *      Mac eth header length
 *
 ********************************************/
#define DRIVER_HANDLER_SIZE		4
#define MAX_MAC_HDR_LEN			26 /* QOS_MAC_HDR_LEN */
#define SUB_MSDU_HEADER_LENGTH		14
#define SNAP_HDR_LEN			8
#define ETHERNET_HDR_LEN		14
#define WORD_ALIGNMENT_PAD		0

#define ETH_ETHERNET_HDR_OFFSET		(MAX_MAC_HDR_LEN + \
					 SUB_MSDU_HEADER_LENGTH + \
					 SNAP_HDR_LEN - \
					 ETHERNET_HDR_LEN + \
					 WORD_ALIGNMENT_PAD)

#define HOST_HDR_OFFSET			4
#define ETHERNET_HDR_LEN		14
#define IP_HDR_LEN			20
#define IP_HDR_OFFSET			ETHERNET_HDR_LEN
#define UDP_HDR_OFFSET			(IP_HDR_LEN + IP_HDR_OFFSET)
#define UDP_HDR_LEN			8
#define UDP_DATA_OFFSET			(UDP_HDR_OFFSET + UDP_HDR_LEN)
#define ETH_CONFIG_PKT_HDR_LEN		UDP_DATA_OFFSET

#define ETH_CONFIG_PKT_HDR_OFFSET	(ETH_ETHERNET_HDR_OFFSET + \
					 ETH_CONFIG_PKT_HDR_LEN)

/********************************************
 *
 *      Register Defines
 *
 ********************************************/
#define WILC_PERIPH_REG_BASE		0x1000
#define WILC_CHANGING_VIR_IF		0x108c
#define WILC_CHIPID			WILC_PERIPH_REG_BASE
#define WILC_GLB_RESET_0		(WILC_PERIPH_REG_BASE + 0x400)
#define WILC_PIN_MUX_0			(WILC_PERIPH_REG_BASE + 0x408)
#define WILC_HOST_TX_CTRL		(WILC_PERIPH_REG_BASE + 0x6c)
#define WILC_HOST_RX_CTRL_0		(WILC_PERIPH_REG_BASE + 0x70)
#define WILC_HOST_RX_CTRL_1		(WILC_PERIPH_REG_BASE + 0x74)
#define WILC_HOST_VMM_CTL		(WILC_PERIPH_REG_BASE + 0x78)
#define WILC_HOST_RX_CTRL		(WILC_PERIPH_REG_BASE + 0x80)
#define WILC_HOST_RX_EXTRA_SIZE		(WILC_PERIPH_REG_BASE + 0x84)
#define WILC_HOST_TX_CTRL_1		(WILC_PERIPH_REG_BASE + 0x88)
#define WILC_MISC			(WILC_PERIPH_REG_BASE + 0x428)
#define WILC_INTR_REG_BASE		(WILC_PERIPH_REG_BASE + 0xa00)
#define WILC_INTR_ENABLE		WILC_INTR_REG_BASE
#define WILC_INTR2_ENABLE		(WILC_INTR_REG_BASE + 4)

#define WILC_INTR_POLARITY		(WILC_INTR_REG_BASE + 0x10)
#define WILC_INTR_TYPE			(WILC_INTR_REG_BASE + 0x20)
#define WILC_INTR_CLEAR			(WILC_INTR_REG_BASE + 0x30)
#define WILC_INTR_STATUS		(WILC_INTR_REG_BASE + 0x40)

#define WILC_VMM_TBL_SIZE		64
#define WILC_VMM_TX_TBL_BASE		0x150400
#define WILC_VMM_RX_TBL_BASE		0x150500

#define WILC_VMM_BASE			0x150000
#define WILC_VMM_CORE_CTL		WILC_VMM_BASE
#define WILC_VMM_TBL_CTL		(WILC_VMM_BASE + 0x4)
#define WILC_VMM_TBL_ENTRY		(WILC_VMM_BASE + 0x8)
#define WILC_VMM_TBL0_SIZE		(WILC_VMM_BASE + 0xc)
#define WILC_VMM_TO_HOST_SIZE		(WILC_VMM_BASE + 0x10)
#define WILC_VMM_CORE_CFG		(WILC_VMM_BASE + 0x14)
#define WILC_VMM_TBL_ACTIVE		(WILC_VMM_BASE + 040)
#define WILC_VMM_TBL_STATUS		(WILC_VMM_BASE + 0x44)

#define WILC_SPI_REG_BASE		0xe800
#define WILC_SPI_CTL			WILC_SPI_REG_BASE
#define WILC_SPI_MASTER_DMA_ADDR	(WILC_SPI_REG_BASE + 0x4)
#define WILC_SPI_MASTER_DMA_COUNT	(WILC_SPI_REG_BASE + 0x8)
#define WILC_SPI_SLAVE_DMA_ADDR		(WILC_SPI_REG_BASE + 0xc)
#define WILC_SPI_SLAVE_DMA_COUNT	(WILC_SPI_REG_BASE + 0x10)
#define WILC_SPI_TX_MODE		(WILC_SPI_REG_BASE + 0x20)
#define WILC_SPI_PROTOCOL_CONFIG	(WILC_SPI_REG_BASE + 0x24)
#define WILC_SPI_INTR_CTL		(WILC_SPI_REG_BASE + 0x2c)

#define WILC_SPI_PROTOCOL_OFFSET	(WILC_SPI_PROTOCOL_CONFIG - \
					 WILC_SPI_REG_BASE)

#define WILC_AHB_DATA_MEM_BASE		0x30000
#define WILC_AHB_SHARE_MEM_BASE		0xd0000

#define WILC_VMM_TBL_RX_SHADOW_BASE	WILC_AHB_SHARE_MEM_BASE
#define WILC_VMM_TBL_RX_SHADOW_SIZE	256

#define WILC_GP_REG_0			0x149c
#define WILC_GP_REG_1			0x14a0

#define WILC_HAVE_SDIO_IRQ_GPIO		BIT(0)
#define WILC_HAVE_USE_PMU		BIT(1)
#define WILC_HAVE_SLEEP_CLK_SRC_RTC	BIT(2)
#define WILC_HAVE_SLEEP_CLK_SRC_XO	BIT(3)
#define WILC_HAVE_EXT_PA_INV_TX_RX	BIT(4)
#define WILC_HAVE_LEGACY_RF_SETTINGS	BIT(5)
#define WILC_HAVE_XTAL_24		BIT(6)
#define WILC_HAVE_DISABLE_WILC_UART	BIT(7)
#define WILC_HAVE_USE_IRQ_AS_HOST_WAKE	BIT(8)

/********************************************
 *
 *      Wlan Defines
 *
 ********************************************/
#define WILC_CFG_PKT		1
#define WILC_NET_PKT		0
#define WILC_MGMT_PKT		2

#define WILC_CFG_SET		1
#define WILC_CFG_QUERY		0

#define WILC_CFG_RSP		1
#define WILC_CFG_RSP_STATUS	2
#define WILC_CFG_RSP_SCAN	3

#define WILC_PLL_TO_SDIO	4
#define WILC_PLL_TO_SPI		2
#define ABORT_INT		BIT(31)

#define LINUX_RX_SIZE		(96 * 1024)
#define LINUX_TX_SIZE		(64 * 1024)

#define MODALIAS		"WILC_SPI"
#define GPIO_NUM		0x44
/*******************************************/
/*        E0 and later Interrupt flags.    */
/*******************************************/
/*******************************************/
/*        E0 and later Interrupt flags.    */
/*           IRQ Status word               */
/* 15:0 = DMA count in words.              */
/* 16: INT0 flag                           */
/* 17: INT1 flag                           */
/* 18: INT2 flag                           */
/* 19: INT3 flag                           */
/* 20: INT4 flag                           */
/* 21: INT5 flag                           */
/*******************************************/
#define IRG_FLAGS_OFFSET	16
#define IRQ_DMA_WD_CNT_MASK	((1ul << IRG_FLAGS_OFFSET) - 1)
#define INT_0			BIT(IRG_FLAGS_OFFSET)
#define INT_1			BIT(IRG_FLAGS_OFFSET + 1)
#define INT_2			BIT(IRG_FLAGS_OFFSET + 2)
#define INT_3			BIT(IRG_FLAGS_OFFSET + 3)
#define INT_4			BIT(IRG_FLAGS_OFFSET + 4)
#define INT_5			BIT(IRG_FLAGS_OFFSET + 5)
#define MAX_NUM_INT		6

/*******************************************/
/*        E0 and later Interrupt flags.    */
/*           IRQ Clear word                */
/* 0: Clear INT0                           */
/* 1: Clear INT1                           */
/* 2: Clear INT2                           */
/* 3: Clear INT3                           */
/* 4: Clear INT4                           */
/* 5: Clear INT5                           */
/* 6: Select VMM table 1                   */
/* 7: Select VMM table 2                   */
/* 8: Enable VMM                           */
/*******************************************/
#define CLR_INT0		BIT(0)
#define CLR_INT1		BIT(1)
#define CLR_INT2		BIT(2)
#define CLR_INT3		BIT(3)
#define CLR_INT4		BIT(4)
#define CLR_INT5		BIT(5)
#define SEL_VMM_TBL0		BIT(6)
#define SEL_VMM_TBL1		BIT(7)
#define EN_VMM			BIT(8)

#define DATA_INT_EXT		INT_0
#define PLL_INT_EXT		INT_1
#define SLEEP_INT_EXT		INT_2
#define ALL_INT_EXT		(DATA_INT_EXT | PLL_INT_EXT | SLEEP_INT_EXT)
#define NUM_INT_EXT		3

#define DATA_INT_CLR		CLR_INT0
#define PLL_INT_CLR		CLR_INT1
#define SLEEP_INT_CLR		CLR_INT2

#define ENABLE_RX_VMM		(SEL_VMM_TBL1 | EN_VMM)
#define ENABLE_TX_VMM		(SEL_VMM_TBL0 | EN_VMM)
/*time for expiring the completion of cfg packets*/
#define CFG_PKTS_TIMEOUT	2000
/********************************************
 *
 *      Debug Type
 *
 ********************************************/
typedef void (*wilc_debug_func)(u32, char *, ...);

/********************************************
 *
 *      Tx/Rx Queue Structure
 *
 ********************************************/

struct txq_entry_t {
	struct txq_entry_t *next;
	struct txq_entry_t *prev;
	int type;
	int tcp_pending_ack_idx;
	u8 *buffer;
	int buffer_size;
	void *priv;
	int status;
	void (*tx_complete_func)(void *, int);
};

struct rxq_entry_t {
	struct rxq_entry_t *next;
	u8 *buffer;
	int buffer_size;
};

/********************************************
 *
 *      Host IF Structure
 *
 ********************************************/
struct wilc;
struct wilc_hif_func {
	int (*hif_init)(struct wilc *, bool resume);
	int (*hif_deinit)(struct wilc *);
	int (*hif_read_reg)(struct wilc *, u32, u32 *);
	int (*hif_write_reg)(struct wilc *, u32, u32);
	int (*hif_block_rx)(struct wilc *, u32, u8 *, u32);
	int (*hif_block_tx)(struct wilc *, u32, u8 *, u32);
	int (*hif_read_int)(struct wilc *, u32 *);
	int (*hif_clear_int_ext)(struct wilc *, u32);
	int (*hif_read_size)(struct wilc *, u32 *);
	int (*hif_block_tx_ext)(struct wilc *, u32, u8 *, u32);
	int (*hif_block_rx_ext)(struct wilc *, u32, u8 *, u32);
	int (*hif_sync_ext)(struct wilc *, int);
	int (*enable_interrupt)(struct wilc *nic);
	void (*disable_interrupt)(struct wilc *nic);
};

/********************************************
 *
 *      Configuration Structure
 *
 ********************************************/

#define MAX_CFG_FRAME_SIZE	1468

struct wilc_cfg_frame {
	u8 ether_header[14];
	u8 ip_header[20];
	u8 udp_header[8];
	u8 wid_header[8];
	u8 frame[MAX_CFG_FRAME_SIZE];
};

struct wilc_cfg_rsp {
	int type;
	u32 seq_no;
};

struct wilc;
struct wilc_vif;

int wilc_wlan_firmware_download(struct wilc *wilc, const u8 *buffer,
				u32 buffer_size);
int wilc_wlan_start(struct wilc *wilc);
int wilc_wlan_stop(struct wilc *wilc);
int wilc_wlan_txq_add_net_pkt(struct net_device *dev, void *priv, u8 *buffer,
			      u32 buffer_size, wilc_tx_complete_func_t func);
int wilc_wlan_handle_txq(struct net_device *dev, u32 *txq_count);
void wilc_handle_isr(struct wilc *wilc);
void wilc_wlan_cleanup(struct net_device *dev);
int wilc_wlan_cfg_set(struct wilc_vif *vif, int start, u16 wid, u8 *buffer,
		      u32 buffer_size, int commit, u32 drv_handler);
int wilc_wlan_cfg_get(struct wilc_vif *vif, int start, u16 wid, int commit,
		      u32 drv_handler);
int wilc_wlan_cfg_get_val(u16 wid, u8 *buffer, u32 buffer_size);
int wilc_wlan_txq_add_mgmt_pkt(struct net_device *dev, void *priv, u8 *buffer,
			       u32 buffer_size, wilc_tx_complete_func_t func);
void wilc_chip_sleep_manually(struct wilc *wilc);

void wilc_enable_tcp_ack_filter(bool value);
int wilc_wlan_get_num_conn_ifcs(struct wilc *wilc);
int wilc_mac_xmit(struct sk_buff *skb, struct net_device *dev);

void WILC_WFI_p2p_rx(struct net_device *dev, u8 *buff, u32 size);
void host_wakeup_notify(struct wilc *wilc);
void host_sleep_notify(struct wilc *wilc);
extern bool wilc_enable_ps;
void chip_allow_sleep(struct wilc *wilc);
void chip_wakeup(struct wilc *wilc);
int wilc_send_config_pkt(struct wilc_vif *vif, u8 mode, struct wid *wids,
			 u32 count, u32 drv);
#endif
