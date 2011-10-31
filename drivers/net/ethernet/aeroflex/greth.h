#ifndef GRETH_H
#define GRETH_H

#include <linux/phy.h>

/* Register bits and masks */
#define GRETH_RESET 0x40
#define GRETH_MII_BUSY 0x8
#define GRETH_MII_NVALID 0x10

#define GRETH_CTRL_FD         0x10
#define GRETH_CTRL_PR         0x20
#define GRETH_CTRL_SP         0x80
#define GRETH_CTRL_GB         0x100
#define GRETH_CTRL_PSTATIEN   0x400
#define GRETH_CTRL_MCEN       0x800
#define GRETH_CTRL_DISDUPLEX  0x1000
#define GRETH_STATUS_PHYSTAT  0x100

#define GRETH_BD_EN 0x800
#define GRETH_BD_WR 0x1000
#define GRETH_BD_IE 0x2000
#define GRETH_BD_LEN 0x7FF

#define GRETH_TXEN 0x1
#define GRETH_INT_TE 0x2
#define GRETH_INT_TX 0x8
#define GRETH_TXI 0x4
#define GRETH_TXBD_STATUS 0x0001C000
#define GRETH_TXBD_MORE 0x20000
#define GRETH_TXBD_IPCS 0x40000
#define GRETH_TXBD_TCPCS 0x80000
#define GRETH_TXBD_UDPCS 0x100000
#define GRETH_TXBD_CSALL (GRETH_TXBD_IPCS | GRETH_TXBD_TCPCS | GRETH_TXBD_UDPCS)
#define GRETH_TXBD_ERR_LC 0x10000
#define GRETH_TXBD_ERR_UE 0x4000
#define GRETH_TXBD_ERR_AL 0x8000

#define GRETH_INT_RE         0x1
#define GRETH_INT_RX         0x4
#define GRETH_RXEN           0x2
#define GRETH_RXI            0x8
#define GRETH_RXBD_STATUS    0xFFFFC000
#define GRETH_RXBD_ERR_AE    0x4000
#define GRETH_RXBD_ERR_FT    0x8000
#define GRETH_RXBD_ERR_CRC   0x10000
#define GRETH_RXBD_ERR_OE    0x20000
#define GRETH_RXBD_ERR_LE    0x40000
#define GRETH_RXBD_IP        0x80000
#define GRETH_RXBD_IP_CSERR  0x100000
#define GRETH_RXBD_UDP       0x200000
#define GRETH_RXBD_UDP_CSERR 0x400000
#define GRETH_RXBD_TCP       0x800000
#define GRETH_RXBD_TCP_CSERR 0x1000000
#define GRETH_RXBD_IP_FRAG   0x2000000
#define GRETH_RXBD_MCAST     0x4000000

/* Descriptor parameters */
#define GRETH_TXBD_NUM 128
#define GRETH_TXBD_NUM_MASK (GRETH_TXBD_NUM-1)
#define GRETH_TX_BUF_SIZE 2048
#define GRETH_RXBD_NUM 128
#define GRETH_RXBD_NUM_MASK (GRETH_RXBD_NUM-1)
#define GRETH_RX_BUF_SIZE 2048

/* Buffers per page */
#define GRETH_RX_BUF_PPGAE	(PAGE_SIZE/GRETH_RX_BUF_SIZE)
#define GRETH_TX_BUF_PPGAE	(PAGE_SIZE/GRETH_TX_BUF_SIZE)

/* How many pages are needed for buffers */
#define GRETH_RX_BUF_PAGE_NUM	(GRETH_RXBD_NUM/GRETH_RX_BUF_PPGAE)
#define GRETH_TX_BUF_PAGE_NUM	(GRETH_TXBD_NUM/GRETH_TX_BUF_PPGAE)

/* Buffer size.
 * Gbit MAC uses tagged maximum frame size which is 1518 excluding CRC.
 * Set to 1520 to make all buffers word aligned for non-gbit MAC.
 */
#define MAX_FRAME_SIZE		1520

/* GRETH APB registers */
struct greth_regs {
	u32 control;
	u32 status;
	u32 esa_msb;
	u32 esa_lsb;
	u32 mdio;
	u32 tx_desc_p;
	u32 rx_desc_p;
	u32 edclip;
	u32 hash_msb;
	u32 hash_lsb;
};

/* GRETH buffer descriptor */
struct greth_bd {
	u32 stat;
	u32 addr;
};

struct greth_private {
	struct sk_buff *rx_skbuff[GRETH_RXBD_NUM];
	struct sk_buff *tx_skbuff[GRETH_TXBD_NUM];

	unsigned char *tx_bufs[GRETH_TXBD_NUM];
	unsigned char *rx_bufs[GRETH_RXBD_NUM];
	u16 tx_bufs_length[GRETH_TXBD_NUM];

	u16 tx_next;
	u16 tx_last;
	u16 tx_free;
	u16 rx_cur;

	struct greth_regs *regs;	/* Address of controller registers. */
	struct greth_bd *rx_bd_base;	/* Address of Rx BDs. */
	struct greth_bd *tx_bd_base;	/* Address of Tx BDs. */
	dma_addr_t rx_bd_base_phys;
	dma_addr_t tx_bd_base_phys;

	int irq;

	struct device *dev;	        /* Pointer to platform_device->dev */
	struct net_device *netdev;
	struct napi_struct napi;
	spinlock_t devlock;

	struct phy_device *phy;
	struct mii_bus *mdio;
	int mdio_irqs[PHY_MAX_ADDR];
	unsigned int link;
	unsigned int speed;
	unsigned int duplex;

	u32 msg_enable;

	u8 phyaddr;
	u8 multicast;
	u8 gbit_mac;
	u8 mdio_int_en;
	u8 edcl;
};

#endif
