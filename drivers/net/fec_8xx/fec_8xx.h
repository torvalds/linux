#ifndef FEC_8XX_H
#define FEC_8XX_H

#include <linux/mii.h>
#include <linux/netdevice.h>

#include <linux/types.h>

/* HW info */

/* CRC polynomium used by the FEC for the multicast group filtering */
#define FEC_CRC_POLY   0x04C11DB7

#define MII_ADVERTISE_HALF	(ADVERTISE_100HALF | \
				 ADVERTISE_10HALF | ADVERTISE_CSMA)
#define MII_ADVERTISE_ALL	(ADVERTISE_100FULL | \
				 ADVERTISE_10FULL | MII_ADVERTISE_HALF)

/* Interrupt events/masks.
*/
#define FEC_ENET_HBERR	0x80000000U	/* Heartbeat error          */
#define FEC_ENET_BABR	0x40000000U	/* Babbling receiver        */
#define FEC_ENET_BABT	0x20000000U	/* Babbling transmitter     */
#define FEC_ENET_GRA	0x10000000U	/* Graceful stop complete   */
#define FEC_ENET_TXF	0x08000000U	/* Full frame transmitted   */
#define FEC_ENET_TXB	0x04000000U	/* A buffer was transmitted */
#define FEC_ENET_RXF	0x02000000U	/* Full frame received      */
#define FEC_ENET_RXB	0x01000000U	/* A buffer was received    */
#define FEC_ENET_MII	0x00800000U	/* MII interrupt            */
#define FEC_ENET_EBERR	0x00400000U	/* SDMA bus error           */

#define FEC_ECNTRL_PINMUX	0x00000004
#define FEC_ECNTRL_ETHER_EN	0x00000002
#define FEC_ECNTRL_RESET	0x00000001

#define FEC_RCNTRL_BC_REJ	0x00000010
#define FEC_RCNTRL_PROM		0x00000008
#define FEC_RCNTRL_MII_MODE	0x00000004
#define FEC_RCNTRL_DRT		0x00000002
#define FEC_RCNTRL_LOOP		0x00000001

#define FEC_TCNTRL_FDEN		0x00000004
#define FEC_TCNTRL_HBC		0x00000002
#define FEC_TCNTRL_GTS		0x00000001

/* values for MII phy_status */

#define PHY_CONF_ANE	0x0001	/* 1 auto-negotiation enabled     */
#define PHY_CONF_LOOP	0x0002	/* 1 loopback mode enabled        */
#define PHY_CONF_SPMASK	0x00f0	/* mask for speed                 */
#define PHY_CONF_10HDX	0x0010	/* 10 Mbit half duplex supported  */
#define PHY_CONF_10FDX	0x0020	/* 10 Mbit full duplex supported  */
#define PHY_CONF_100HDX	0x0040	/* 100 Mbit half duplex supported */
#define PHY_CONF_100FDX	0x0080	/* 100 Mbit full duplex supported */

#define PHY_STAT_LINK	0x0100	/* 1 up - 0 down                  */
#define PHY_STAT_FAULT	0x0200	/* 1 remote fault                 */
#define PHY_STAT_ANC	0x0400	/* 1 auto-negotiation complete    */
#define PHY_STAT_SPMASK	0xf000	/* mask for speed                 */
#define PHY_STAT_10HDX	0x1000	/* 10 Mbit half duplex selected   */
#define PHY_STAT_10FDX	0x2000	/* 10 Mbit full duplex selected   */
#define PHY_STAT_100HDX	0x4000	/* 100 Mbit half duplex selected  */
#define PHY_STAT_100FDX	0x8000	/* 100 Mbit full duplex selected  */

typedef struct phy_info {
	unsigned int id;
	const char *name;
	void (*startup) (struct net_device * dev);
	void (*shutdown) (struct net_device * dev);
	void (*ack_int) (struct net_device * dev);
} phy_info_t;

/* The FEC stores dest/src/type, data, and checksum for receive packets.
 */
#define MAX_MTU 1508		/* Allow fullsized pppoe packets over VLAN */
#define MIN_MTU 46		/* this is data size */
#define CRC_LEN 4

#define PKT_MAXBUF_SIZE		(MAX_MTU+ETH_HLEN+CRC_LEN)
#define PKT_MINBUF_SIZE		(MIN_MTU+ETH_HLEN+CRC_LEN)

/* Must be a multiple of 4 */
#define PKT_MAXBLR_SIZE		((PKT_MAXBUF_SIZE+3) & ~3)
/* This is needed so that invalidate_xxx wont invalidate too much */
#define ENET_RX_FRSIZE		L1_CACHE_ALIGN(PKT_MAXBUF_SIZE)

/* platform interface */

struct fec_platform_info {
	int fec_no;		/* FEC index                  */
	int use_mdio;		/* use external MII           */
	int phy_addr;		/* the phy address            */
	int fec_irq, phy_irq;	/* the irq for the controller */
	int rx_ring, tx_ring;	/* number of buffers on rx    */
	int sys_clk;		/* system clock               */
	__u8 macaddr[6];	/* mac address                */
	int rx_copybreak;	/* limit we copy small frames */
	int use_napi;		/* use NAPI                   */
	int napi_weight;	/* NAPI weight                */
};

/* forward declaration */
struct fec;

struct fec_enet_private {
	spinlock_t lock;	/* during all ops except TX pckt processing */
	spinlock_t tx_lock;	/* during fec_start_xmit and fec_tx         */
	struct net_device *dev;
	struct napi_struct napi;
	int fecno;
	struct fec *fecp;
	const struct fec_platform_info *fpi;
	int rx_ring, tx_ring;
	dma_addr_t ring_mem_addr;
	void *ring_base;
	struct sk_buff **rx_skbuff;
	struct sk_buff **tx_skbuff;
	cbd_t *rx_bd_base;	/* Address of Rx and Tx buffers.    */
	cbd_t *tx_bd_base;
	cbd_t *dirty_tx;	/* ring entries to be free()ed.     */
	cbd_t *cur_rx;
	cbd_t *cur_tx;
	int tx_free;
	struct net_device_stats stats;
	struct timer_list phy_timer_list;
	const struct phy_info *phy;
	unsigned int fec_phy_speed;
	__u32 msg_enable;
	struct mii_if_info mii_if;
};

/***************************************************************************/

void fec_restart(struct net_device *dev, int duplex, int speed);
void fec_stop(struct net_device *dev);

/***************************************************************************/

int fec_mii_read(struct net_device *dev, int phy_id, int location);
void fec_mii_write(struct net_device *dev, int phy_id, int location, int value);

int fec_mii_phy_id_detect(struct net_device *dev);
void fec_mii_startup(struct net_device *dev);
void fec_mii_shutdown(struct net_device *dev);
void fec_mii_ack_int(struct net_device *dev);

void fec_mii_link_status_change_check(struct net_device *dev, int init_media);

/***************************************************************************/

#define FEC1_NO	0x00
#define FEC2_NO	0x01
#define FEC3_NO	0x02

int fec_8xx_init_one(const struct fec_platform_info *fpi,
		     struct net_device **devp);
int fec_8xx_cleanup_one(struct net_device *dev);

/***************************************************************************/

#define DRV_MODULE_NAME		"fec_8xx"
#define PFX DRV_MODULE_NAME	": "
#define DRV_MODULE_VERSION	"0.1"
#define DRV_MODULE_RELDATE	"May 6, 2004"

/***************************************************************************/

int fec_8xx_platform_init(void);
void fec_8xx_platform_cleanup(void);

/***************************************************************************/

/* FEC access macros */
#if defined(CONFIG_8xx)
/* for a 8xx __raw_xxx's are sufficient */
#define __fec_out32(addr, x)	__raw_writel(x, addr)
#define __fec_out16(addr, x)	__raw_writew(x, addr)
#define __fec_in32(addr)	__raw_readl(addr)
#define __fec_in16(addr)	__raw_readw(addr)
#else
/* for others play it safe */
#define __fec_out32(addr, x)	out_be32(addr, x)
#define __fec_out16(addr, x)	out_be16(addr, x)
#define __fec_in32(addr)	in_be32(addr)
#define __fec_in16(addr)	in_be16(addr)
#endif

/* write */
#define FW(_fecp, _reg, _v) __fec_out32(&(_fecp)->fec_ ## _reg, (_v))

/* read */
#define FR(_fecp, _reg)	__fec_in32(&(_fecp)->fec_ ## _reg)

/* set bits */
#define FS(_fecp, _reg, _v) FW(_fecp, _reg, FR(_fecp, _reg) | (_v))

/* clear bits */
#define FC(_fecp, _reg, _v) FW(_fecp, _reg, FR(_fecp, _reg) & ~(_v))

/* buffer descriptor access macros */

/* write */
#define CBDW_SC(_cbd, _sc) 		__fec_out16(&(_cbd)->cbd_sc, (_sc))
#define CBDW_DATLEN(_cbd, _datlen)	__fec_out16(&(_cbd)->cbd_datlen, (_datlen))
#define CBDW_BUFADDR(_cbd, _bufaddr)	__fec_out32(&(_cbd)->cbd_bufaddr, (_bufaddr))

/* read */
#define CBDR_SC(_cbd) 			__fec_in16(&(_cbd)->cbd_sc)
#define CBDR_DATLEN(_cbd)		__fec_in16(&(_cbd)->cbd_datlen)
#define CBDR_BUFADDR(_cbd)		__fec_in32(&(_cbd)->cbd_bufaddr)

/* set bits */
#define CBDS_SC(_cbd, _sc) 		CBDW_SC(_cbd, CBDR_SC(_cbd) | (_sc))

/* clear bits */
#define CBDC_SC(_cbd, _sc) 		CBDW_SC(_cbd, CBDR_SC(_cbd) & ~(_sc))

/***************************************************************************/

#endif
