#ifndef FS_ENET_H
#define FS_ENET_H

#include <linux/mii.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/phy.h>
#include <linux/dma-mapping.h>

#include <linux/fs_enet_pd.h>

#ifdef CONFIG_CPM1
#include <asm/commproc.h>

struct fec_info {
        fec_t*  fecp;
	u32     mii_speed;
};
#endif

#ifdef CONFIG_CPM2
#include <asm/cpm2.h>
#endif

/* This is used to operate with pins.
  Note that the actual port size may
    be different; cpm(s) handle it OK  */
struct bb_info {
	u8 mdio_dat_msk;
	u8 mdio_dir_msk;
	u8 *mdio_dir;
	u8 *mdio_dat;
	u8 mdc_msk;
	u8 *mdc_dat;
	int delay;
};

/* hw driver ops */
struct fs_ops {
	int (*setup_data)(struct net_device *dev);
	int (*allocate_bd)(struct net_device *dev);
	void (*free_bd)(struct net_device *dev);
	void (*cleanup_data)(struct net_device *dev);
	void (*set_multicast_list)(struct net_device *dev);
	void (*adjust_link)(struct net_device *dev);
	void (*restart)(struct net_device *dev);
	void (*stop)(struct net_device *dev);
	void (*pre_request_irq)(struct net_device *dev, int irq);
	void (*post_free_irq)(struct net_device *dev, int irq);
	void (*napi_clear_rx_event)(struct net_device *dev);
	void (*napi_enable_rx)(struct net_device *dev);
	void (*napi_disable_rx)(struct net_device *dev);
	void (*rx_bd_done)(struct net_device *dev);
	void (*tx_kickstart)(struct net_device *dev);
	u32 (*get_int_events)(struct net_device *dev);
	void (*clear_int_events)(struct net_device *dev, u32 int_events);
	void (*ev_error)(struct net_device *dev, u32 int_events);
	int (*get_regs)(struct net_device *dev, void *p, int *sizep);
	int (*get_regs_len)(struct net_device *dev);
	void (*tx_restart)(struct net_device *dev);
};

struct phy_info {
	unsigned int id;
	const char *name;
	void (*startup) (struct net_device * dev);
	void (*shutdown) (struct net_device * dev);
	void (*ack_int) (struct net_device * dev);
};

/* The FEC stores dest/src/type, data, and checksum for receive packets.
 */
#define MAX_MTU 1508		/* Allow fullsized pppoe packets over VLAN */
#define MIN_MTU 46		/* this is data size */
#define CRC_LEN 4

#define PKT_MAXBUF_SIZE		(MAX_MTU+ETH_HLEN+CRC_LEN)
#define PKT_MINBUF_SIZE		(MIN_MTU+ETH_HLEN+CRC_LEN)

/* Must be a multiple of 32 (to cover both FEC & FCC) */
#define PKT_MAXBLR_SIZE		((PKT_MAXBUF_SIZE + 31) & ~31)
/* This is needed so that invalidate_xxx wont invalidate too much */
#define ENET_RX_FRSIZE		L1_CACHE_ALIGN(PKT_MAXBUF_SIZE)

struct fs_enet_mii_bus {
	struct list_head list;
	spinlock_t mii_lock;
	const struct fs_mii_bus_info *bus_info;
	int refs;
	u32 usage_map;

	int (*mii_read)(struct fs_enet_mii_bus *bus,
			int phy_id, int location);

	void (*mii_write)(struct fs_enet_mii_bus *bus,
			int phy_id, int location, int value);

	union {
		struct {
			unsigned int mii_speed;
			void *fecp;
		} fec;

		struct {
			/* note that the actual port size may */
			/* be different; cpm(s) handle it OK  */
			u8 mdio_msk;
			u8 *mdio_dir;
			u8 *mdio_dat;
			u8 mdc_msk;
			u8 *mdc_dir;
			u8 *mdc_dat;
		} bitbang;

		struct {
			u16 lpa;
		} fixed;
	};
};

struct fs_enet_private {
	struct device *dev;	/* pointer back to the device (must be initialized first) */
	spinlock_t lock;	/* during all ops except TX pckt processing */
	spinlock_t tx_lock;	/* during fs_start_xmit and fs_tx         */
	const struct fs_platform_info *fpi;
	const struct fs_ops *ops;
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
	u32 msg_enable;
	struct mii_if_info mii_if;
	unsigned int last_mii_status;
	struct fs_enet_mii_bus *mii_bus;
	int interrupt;

	struct phy_device *phydev;
	int oldduplex, oldspeed, oldlink;	/* current settings */

	/* event masks */
	u32 ev_napi_rx;		/* mask of NAPI rx events */
	u32 ev_rx;		/* rx event mask          */
	u32 ev_tx;		/* tx event mask          */
	u32 ev_err;		/* error event mask       */

	u16 bd_rx_empty;	/* mask of BD rx empty	  */
	u16 bd_rx_err;		/* mask of BD rx errors   */

	union {
		struct {
			int idx;		/* FEC1 = 0, FEC2 = 1  */
			void *fecp;		/* hw registers        */
			u32 hthi, htlo;		/* state for multicast */
		} fec;

		struct {
			int idx;		/* FCC1-3 = 0-2	       */
			void *fccp;		/* hw registers	       */
			void *ep;		/* parameter ram       */
			void *fcccp;		/* hw registers cont.  */
			void *mem;		/* FCC DPRAM */
			u32 gaddrh, gaddrl;	/* group address       */
		} fcc;

		struct {
			int idx;		/* FEC1 = 0, FEC2 = 1  */
			void *sccp;		/* hw registers        */
			void *ep;		/* parameter ram       */
			u32 hthi, htlo;		/* state for multicast */
		} scc;

	};
};

/***************************************************************************/
int fs_enet_mdio_bb_init(void);
int fs_mii_fixed_init(struct fs_enet_mii_bus *bus);
int fs_enet_mdio_fec_init(void);

void fs_init_bds(struct net_device *dev);
void fs_cleanup_bds(struct net_device *dev);

/***************************************************************************/

#define DRV_MODULE_NAME		"fs_enet"
#define PFX DRV_MODULE_NAME	": "
#define DRV_MODULE_VERSION	"1.0"
#define DRV_MODULE_RELDATE	"Aug 8, 2005"

/***************************************************************************/

int fs_enet_platform_init(void);
void fs_enet_platform_cleanup(void);

/***************************************************************************/
/* buffer descriptor access macros */

/* access macros */
#if defined(CONFIG_CPM1)
/* for a a CPM1 __raw_xxx's are sufficient */
#define __cbd_out32(addr, x)	__raw_writel(x, addr)
#define __cbd_out16(addr, x)	__raw_writew(x, addr)
#define __cbd_in32(addr)	__raw_readl(addr)
#define __cbd_in16(addr)	__raw_readw(addr)
#else
/* for others play it safe */
#define __cbd_out32(addr, x)	out_be32(addr, x)
#define __cbd_out16(addr, x)	out_be16(addr, x)
#define __cbd_in32(addr)	in_be32(addr)
#define __cbd_in16(addr)	in_be16(addr)
#endif

/* write */
#define CBDW_SC(_cbd, _sc) 		__cbd_out16(&(_cbd)->cbd_sc, (_sc))
#define CBDW_DATLEN(_cbd, _datlen)	__cbd_out16(&(_cbd)->cbd_datlen, (_datlen))
#define CBDW_BUFADDR(_cbd, _bufaddr)	__cbd_out32(&(_cbd)->cbd_bufaddr, (_bufaddr))

/* read */
#define CBDR_SC(_cbd) 			__cbd_in16(&(_cbd)->cbd_sc)
#define CBDR_DATLEN(_cbd)		__cbd_in16(&(_cbd)->cbd_datlen)
#define CBDR_BUFADDR(_cbd)		__cbd_in32(&(_cbd)->cbd_bufaddr)

/* set bits */
#define CBDS_SC(_cbd, _sc) 		CBDW_SC(_cbd, CBDR_SC(_cbd) | (_sc))

/* clear bits */
#define CBDC_SC(_cbd, _sc) 		CBDW_SC(_cbd, CBDR_SC(_cbd) & ~(_sc))

/*******************************************************************/

extern const struct fs_ops fs_fec_ops;
extern const struct fs_ops fs_fcc_ops;
extern const struct fs_ops fs_scc_ops;

/*******************************************************************/

/* handy pointer to the immap */
extern void *fs_enet_immap;

/*******************************************************************/

#endif
