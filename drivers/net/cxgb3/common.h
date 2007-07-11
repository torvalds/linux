/*
 * Copyright (c) 2005-2007 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef __CHELSIO_COMMON_H
#define __CHELSIO_COMMON_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include "version.h"

#define CH_ERR(adap, fmt, ...)   dev_err(&adap->pdev->dev, fmt, ## __VA_ARGS__)
#define CH_WARN(adap, fmt, ...)  dev_warn(&adap->pdev->dev, fmt, ## __VA_ARGS__)
#define CH_ALERT(adap, fmt, ...) \
	dev_printk(KERN_ALERT, &adap->pdev->dev, fmt, ## __VA_ARGS__)

/*
 * More powerful macro that selectively prints messages based on msg_enable.
 * For info and debugging messages.
 */
#define CH_MSG(adapter, level, category, fmt, ...) do { \
	if ((adapter)->msg_enable & NETIF_MSG_##category) \
		dev_printk(KERN_##level, &adapter->pdev->dev, fmt, \
			   ## __VA_ARGS__); \
} while (0)

#ifdef DEBUG
# define CH_DBG(adapter, category, fmt, ...) \
	CH_MSG(adapter, DEBUG, category, fmt, ## __VA_ARGS__)
#else
# define CH_DBG(adapter, category, fmt, ...)
#endif

/* Additional NETIF_MSG_* categories */
#define NETIF_MSG_MMIO 0x8000000

struct t3_rx_mode {
	struct net_device *dev;
	struct dev_mc_list *mclist;
	unsigned int idx;
};

static inline void init_rx_mode(struct t3_rx_mode *p, struct net_device *dev,
				struct dev_mc_list *mclist)
{
	p->dev = dev;
	p->mclist = mclist;
	p->idx = 0;
}

static inline u8 *t3_get_next_mcaddr(struct t3_rx_mode *rm)
{
	u8 *addr = NULL;

	if (rm->mclist && rm->idx < rm->dev->mc_count) {
		addr = rm->mclist->dmi_addr;
		rm->mclist = rm->mclist->next;
		rm->idx++;
	}
	return addr;
}

enum {
	MAX_NPORTS = 2,		/* max # of ports */
	MAX_FRAME_SIZE = 10240,	/* max MAC frame size, including header + FCS */
	EEPROMSIZE = 8192,	/* Serial EEPROM size */
	RSS_TABLE_SIZE = 64,	/* size of RSS lookup and mapping tables */
	TCB_SIZE = 128,		/* TCB size */
	NMTUS = 16,		/* size of MTU table */
	NCCTRL_WIN = 32,	/* # of congestion control windows */
	PROTO_SRAM_LINES = 128, /* size of TP sram */
};

#define MAX_RX_COALESCING_LEN 16224U

enum {
	PAUSE_RX = 1 << 0,
	PAUSE_TX = 1 << 1,
	PAUSE_AUTONEG = 1 << 2
};

enum {
	SUPPORTED_IRQ      = 1 << 24
};

enum {				/* adapter interrupt-maintained statistics */
	STAT_ULP_CH0_PBL_OOB,
	STAT_ULP_CH1_PBL_OOB,
	STAT_PCI_CORR_ECC,

	IRQ_NUM_STATS		/* keep last */
};

enum {
	TP_VERSION_MAJOR	= 1,
	TP_VERSION_MINOR	= 0,
	TP_VERSION_MICRO	= 44
};

#define S_TP_VERSION_MAJOR		16
#define M_TP_VERSION_MAJOR		0xFF
#define V_TP_VERSION_MAJOR(x)		((x) << S_TP_VERSION_MAJOR)
#define G_TP_VERSION_MAJOR(x)		\
	    (((x) >> S_TP_VERSION_MAJOR) & M_TP_VERSION_MAJOR)

#define S_TP_VERSION_MINOR		8
#define M_TP_VERSION_MINOR		0xFF
#define V_TP_VERSION_MINOR(x)		((x) << S_TP_VERSION_MINOR)
#define G_TP_VERSION_MINOR(x)		\
	    (((x) >> S_TP_VERSION_MINOR) & M_TP_VERSION_MINOR)

#define S_TP_VERSION_MICRO		0
#define M_TP_VERSION_MICRO		0xFF
#define V_TP_VERSION_MICRO(x)		((x) << S_TP_VERSION_MICRO)
#define G_TP_VERSION_MICRO(x)		\
	    (((x) >> S_TP_VERSION_MICRO) & M_TP_VERSION_MICRO)

enum {
	SGE_QSETS = 8,		/* # of SGE Tx/Rx/RspQ sets */
	SGE_RXQ_PER_SET = 2,	/* # of Rx queues per set */
	SGE_TXQ_PER_SET = 3	/* # of Tx queues per set */
};

enum sge_context_type {		/* SGE egress context types */
	SGE_CNTXT_RDMA = 0,
	SGE_CNTXT_ETH = 2,
	SGE_CNTXT_OFLD = 4,
	SGE_CNTXT_CTRL = 5
};

enum {
	AN_PKT_SIZE = 32,	/* async notification packet size */
	IMMED_PKT_SIZE = 48	/* packet size for immediate data */
};

struct sg_ent {			/* SGE scatter/gather entry */
	u32 len[2];
	u64 addr[2];
};

#ifndef SGE_NUM_GENBITS
/* Must be 1 or 2 */
# define SGE_NUM_GENBITS 2
#endif

#define TX_DESC_FLITS 16U
#define WR_FLITS (TX_DESC_FLITS + 1 - SGE_NUM_GENBITS)

struct cphy;
struct adapter;

struct mdio_ops {
	int (*read)(struct adapter *adapter, int phy_addr, int mmd_addr,
		    int reg_addr, unsigned int *val);
	int (*write)(struct adapter *adapter, int phy_addr, int mmd_addr,
		     int reg_addr, unsigned int val);
};

struct adapter_info {
	unsigned char nports;	/* # of ports */
	unsigned char phy_base_addr;	/* MDIO PHY base address */
	unsigned char mdien;
	unsigned char mdiinv;
	unsigned int gpio_out;	/* GPIO output settings */
	unsigned int gpio_intr;	/* GPIO IRQ enable mask */
	unsigned long caps;	/* adapter capabilities */
	const struct mdio_ops *mdio_ops;	/* MDIO operations */
	const char *desc;	/* product description */
};

struct port_type_info {
	void (*phy_prep)(struct cphy *phy, struct adapter *adapter,
			 int phy_addr, const struct mdio_ops *ops);
	unsigned int caps;
	const char *desc;
};

struct mc5_stats {
	unsigned long parity_err;
	unsigned long active_rgn_full;
	unsigned long nfa_srch_err;
	unsigned long unknown_cmd;
	unsigned long reqq_parity_err;
	unsigned long dispq_parity_err;
	unsigned long del_act_empty;
};

struct mc7_stats {
	unsigned long corr_err;
	unsigned long uncorr_err;
	unsigned long parity_err;
	unsigned long addr_err;
};

struct mac_stats {
	u64 tx_octets;		/* total # of octets in good frames */
	u64 tx_octets_bad;	/* total # of octets in error frames */
	u64 tx_frames;		/* all good frames */
	u64 tx_mcast_frames;	/* good multicast frames */
	u64 tx_bcast_frames;	/* good broadcast frames */
	u64 tx_pause;		/* # of transmitted pause frames */
	u64 tx_deferred;	/* frames with deferred transmissions */
	u64 tx_late_collisions;	/* # of late collisions */
	u64 tx_total_collisions;	/* # of total collisions */
	u64 tx_excess_collisions;	/* frame errors from excessive collissions */
	u64 tx_underrun;	/* # of Tx FIFO underruns */
	u64 tx_len_errs;	/* # of Tx length errors */
	u64 tx_mac_internal_errs;	/* # of internal MAC errors on Tx */
	u64 tx_excess_deferral;	/* # of frames with excessive deferral */
	u64 tx_fcs_errs;	/* # of frames with bad FCS */

	u64 tx_frames_64;	/* # of Tx frames in a particular range */
	u64 tx_frames_65_127;
	u64 tx_frames_128_255;
	u64 tx_frames_256_511;
	u64 tx_frames_512_1023;
	u64 tx_frames_1024_1518;
	u64 tx_frames_1519_max;

	u64 rx_octets;		/* total # of octets in good frames */
	u64 rx_octets_bad;	/* total # of octets in error frames */
	u64 rx_frames;		/* all good frames */
	u64 rx_mcast_frames;	/* good multicast frames */
	u64 rx_bcast_frames;	/* good broadcast frames */
	u64 rx_pause;		/* # of received pause frames */
	u64 rx_fcs_errs;	/* # of received frames with bad FCS */
	u64 rx_align_errs;	/* alignment errors */
	u64 rx_symbol_errs;	/* symbol errors */
	u64 rx_data_errs;	/* data errors */
	u64 rx_sequence_errs;	/* sequence errors */
	u64 rx_runt;		/* # of runt frames */
	u64 rx_jabber;		/* # of jabber frames */
	u64 rx_short;		/* # of short frames */
	u64 rx_too_long;	/* # of oversized frames */
	u64 rx_mac_internal_errs;	/* # of internal MAC errors on Rx */

	u64 rx_frames_64;	/* # of Rx frames in a particular range */
	u64 rx_frames_65_127;
	u64 rx_frames_128_255;
	u64 rx_frames_256_511;
	u64 rx_frames_512_1023;
	u64 rx_frames_1024_1518;
	u64 rx_frames_1519_max;

	u64 rx_cong_drops;	/* # of Rx drops due to SGE congestion */

	unsigned long tx_fifo_parity_err;
	unsigned long rx_fifo_parity_err;
	unsigned long tx_fifo_urun;
	unsigned long rx_fifo_ovfl;
	unsigned long serdes_signal_loss;
	unsigned long xaui_pcs_ctc_err;
	unsigned long xaui_pcs_align_change;

	unsigned long num_toggled; /* # times toggled TxEn due to stuck TX */
	unsigned long num_resets;  /* # times reset due to stuck TX */

};

struct tp_mib_stats {
	u32 ipInReceive_hi;
	u32 ipInReceive_lo;
	u32 ipInHdrErrors_hi;
	u32 ipInHdrErrors_lo;
	u32 ipInAddrErrors_hi;
	u32 ipInAddrErrors_lo;
	u32 ipInUnknownProtos_hi;
	u32 ipInUnknownProtos_lo;
	u32 ipInDiscards_hi;
	u32 ipInDiscards_lo;
	u32 ipInDelivers_hi;
	u32 ipInDelivers_lo;
	u32 ipOutRequests_hi;
	u32 ipOutRequests_lo;
	u32 ipOutDiscards_hi;
	u32 ipOutDiscards_lo;
	u32 ipOutNoRoutes_hi;
	u32 ipOutNoRoutes_lo;
	u32 ipReasmTimeout;
	u32 ipReasmReqds;
	u32 ipReasmOKs;
	u32 ipReasmFails;

	u32 reserved[8];

	u32 tcpActiveOpens;
	u32 tcpPassiveOpens;
	u32 tcpAttemptFails;
	u32 tcpEstabResets;
	u32 tcpOutRsts;
	u32 tcpCurrEstab;
	u32 tcpInSegs_hi;
	u32 tcpInSegs_lo;
	u32 tcpOutSegs_hi;
	u32 tcpOutSegs_lo;
	u32 tcpRetransSeg_hi;
	u32 tcpRetransSeg_lo;
	u32 tcpInErrs_hi;
	u32 tcpInErrs_lo;
	u32 tcpRtoMin;
	u32 tcpRtoMax;
};

struct tp_params {
	unsigned int nchan;	/* # of channels */
	unsigned int pmrx_size;	/* total PMRX capacity */
	unsigned int pmtx_size;	/* total PMTX capacity */
	unsigned int cm_size;	/* total CM capacity */
	unsigned int chan_rx_size;	/* per channel Rx size */
	unsigned int chan_tx_size;	/* per channel Tx size */
	unsigned int rx_pg_size;	/* Rx page size */
	unsigned int tx_pg_size;	/* Tx page size */
	unsigned int rx_num_pgs;	/* # of Rx pages */
	unsigned int tx_num_pgs;	/* # of Tx pages */
	unsigned int ntimer_qs;	/* # of timer queues */
};

struct qset_params {		/* SGE queue set parameters */
	unsigned int polling;	/* polling/interrupt service for rspq */
	unsigned int coalesce_usecs;	/* irq coalescing timer */
	unsigned int rspq_size;	/* # of entries in response queue */
	unsigned int fl_size;	/* # of entries in regular free list */
	unsigned int jumbo_size;	/* # of entries in jumbo free list */
	unsigned int txq_size[SGE_TXQ_PER_SET];	/* Tx queue sizes */
	unsigned int cong_thres;	/* FL congestion threshold */
};

struct sge_params {
	unsigned int max_pkt_size;	/* max offload pkt size */
	struct qset_params qset[SGE_QSETS];
};

struct mc5_params {
	unsigned int mode;	/* selects MC5 width */
	unsigned int nservers;	/* size of server region */
	unsigned int nfilters;	/* size of filter region */
	unsigned int nroutes;	/* size of routing region */
};

/* Default MC5 region sizes */
enum {
	DEFAULT_NSERVERS = 512,
	DEFAULT_NFILTERS = 128
};

/* MC5 modes, these must be non-0 */
enum {
	MC5_MODE_144_BIT = 1,
	MC5_MODE_72_BIT = 2
};

/* MC5 min active region size */
enum { MC5_MIN_TIDS = 16 };

struct vpd_params {
	unsigned int cclk;
	unsigned int mclk;
	unsigned int uclk;
	unsigned int mdc;
	unsigned int mem_timing;
	u8 eth_base[6];
	u8 port_type[MAX_NPORTS];
	unsigned short xauicfg[2];
};

struct pci_params {
	unsigned int vpd_cap_addr;
	unsigned int pcie_cap_addr;
	unsigned short speed;
	unsigned char width;
	unsigned char variant;
};

enum {
	PCI_VARIANT_PCI,
	PCI_VARIANT_PCIX_MODE1_PARITY,
	PCI_VARIANT_PCIX_MODE1_ECC,
	PCI_VARIANT_PCIX_266_MODE2,
	PCI_VARIANT_PCIE
};

struct adapter_params {
	struct sge_params sge;
	struct mc5_params mc5;
	struct tp_params tp;
	struct vpd_params vpd;
	struct pci_params pci;

	const struct adapter_info *info;

	unsigned short mtus[NMTUS];
	unsigned short a_wnd[NCCTRL_WIN];
	unsigned short b_wnd[NCCTRL_WIN];

	unsigned int nports;	/* # of ethernet ports */
	unsigned int stats_update_period;	/* MAC stats accumulation period */
	unsigned int linkpoll_period;	/* link poll period in 0.1s */
	unsigned int rev;	/* chip revision */
	unsigned int offload;
};

enum {					    /* chip revisions */
	T3_REV_A  = 0,
	T3_REV_B  = 2,
	T3_REV_B2 = 3,
};

struct trace_params {
	u32 sip;
	u32 sip_mask;
	u32 dip;
	u32 dip_mask;
	u16 sport;
	u16 sport_mask;
	u16 dport;
	u16 dport_mask;
	u32 vlan:12;
	u32 vlan_mask:12;
	u32 intf:4;
	u32 intf_mask:4;
	u8 proto;
	u8 proto_mask;
};

struct link_config {
	unsigned int supported;	/* link capabilities */
	unsigned int advertising;	/* advertised capabilities */
	unsigned short requested_speed;	/* speed user has requested */
	unsigned short speed;	/* actual link speed */
	unsigned char requested_duplex;	/* duplex user has requested */
	unsigned char duplex;	/* actual link duplex */
	unsigned char requested_fc;	/* flow control user has requested */
	unsigned char fc;	/* actual link flow control */
	unsigned char autoneg;	/* autonegotiating? */
	unsigned int link_ok;	/* link up? */
};

#define SPEED_INVALID   0xffff
#define DUPLEX_INVALID  0xff

struct mc5 {
	struct adapter *adapter;
	unsigned int tcam_size;
	unsigned char part_type;
	unsigned char parity_enabled;
	unsigned char mode;
	struct mc5_stats stats;
};

static inline unsigned int t3_mc5_size(const struct mc5 *p)
{
	return p->tcam_size;
}

struct mc7 {
	struct adapter *adapter;	/* backpointer to adapter */
	unsigned int size;	/* memory size in bytes */
	unsigned int width;	/* MC7 interface width */
	unsigned int offset;	/* register address offset for MC7 instance */
	const char *name;	/* name of MC7 instance */
	struct mc7_stats stats;	/* MC7 statistics */
};

static inline unsigned int t3_mc7_size(const struct mc7 *p)
{
	return p->size;
}

struct cmac {
	struct adapter *adapter;
	unsigned int offset;
	unsigned int nucast;	/* # of address filters for unicast MACs */
	unsigned int tx_tcnt;
	unsigned int tx_xcnt;
	u64 tx_mcnt;
	unsigned int rx_xcnt;
	u64 rx_mcnt;
	unsigned int toggle_cnt;
	unsigned int txen;
	struct mac_stats stats;
};

enum {
	MAC_DIRECTION_RX = 1,
	MAC_DIRECTION_TX = 2,
	MAC_RXFIFO_SIZE = 32768
};

/* IEEE 802.3ae specified MDIO devices */
enum {
	MDIO_DEV_PMA_PMD = 1,
	MDIO_DEV_WIS = 2,
	MDIO_DEV_PCS = 3,
	MDIO_DEV_XGXS = 4
};

/* PHY loopback direction */
enum {
	PHY_LOOPBACK_TX = 1,
	PHY_LOOPBACK_RX = 2
};

/* PHY interrupt types */
enum {
	cphy_cause_link_change = 1,
	cphy_cause_fifo_error = 2
};

/* PHY operations */
struct cphy_ops {
	void (*destroy)(struct cphy *phy);
	int (*reset)(struct cphy *phy, int wait);

	int (*intr_enable)(struct cphy *phy);
	int (*intr_disable)(struct cphy *phy);
	int (*intr_clear)(struct cphy *phy);
	int (*intr_handler)(struct cphy *phy);

	int (*autoneg_enable)(struct cphy *phy);
	int (*autoneg_restart)(struct cphy *phy);

	int (*advertise)(struct cphy *phy, unsigned int advertise_map);
	int (*set_loopback)(struct cphy *phy, int mmd, int dir, int enable);
	int (*set_speed_duplex)(struct cphy *phy, int speed, int duplex);
	int (*get_link_status)(struct cphy *phy, int *link_ok, int *speed,
			       int *duplex, int *fc);
	int (*power_down)(struct cphy *phy, int enable);
};

/* A PHY instance */
struct cphy {
	int addr;		/* PHY address */
	struct adapter *adapter;	/* associated adapter */
	unsigned long fifo_errors;	/* FIFO over/under-flows */
	const struct cphy_ops *ops;	/* PHY operations */
	int (*mdio_read)(struct adapter *adapter, int phy_addr, int mmd_addr,
			 int reg_addr, unsigned int *val);
	int (*mdio_write)(struct adapter *adapter, int phy_addr, int mmd_addr,
			  int reg_addr, unsigned int val);
};

/* Convenience MDIO read/write wrappers */
static inline int mdio_read(struct cphy *phy, int mmd, int reg,
			    unsigned int *valp)
{
	return phy->mdio_read(phy->adapter, phy->addr, mmd, reg, valp);
}

static inline int mdio_write(struct cphy *phy, int mmd, int reg,
			     unsigned int val)
{
	return phy->mdio_write(phy->adapter, phy->addr, mmd, reg, val);
}

/* Convenience initializer */
static inline void cphy_init(struct cphy *phy, struct adapter *adapter,
			     int phy_addr, struct cphy_ops *phy_ops,
			     const struct mdio_ops *mdio_ops)
{
	phy->adapter = adapter;
	phy->addr = phy_addr;
	phy->ops = phy_ops;
	if (mdio_ops) {
		phy->mdio_read = mdio_ops->read;
		phy->mdio_write = mdio_ops->write;
	}
}

/* Accumulate MAC statistics every 180 seconds.  For 1G we multiply by 10. */
#define MAC_STATS_ACCUM_SECS 180

#define XGM_REG(reg_addr, idx) \
	((reg_addr) + (idx) * (XGMAC0_1_BASE_ADDR - XGMAC0_0_BASE_ADDR))

struct addr_val_pair {
	unsigned int reg_addr;
	unsigned int val;
};

#include "adapter.h"

#ifndef PCI_VENDOR_ID_CHELSIO
# define PCI_VENDOR_ID_CHELSIO 0x1425
#endif

#define for_each_port(adapter, iter) \
	for (iter = 0; iter < (adapter)->params.nports; ++iter)

#define adapter_info(adap) ((adap)->params.info)

static inline int uses_xaui(const struct adapter *adap)
{
	return adapter_info(adap)->caps & SUPPORTED_AUI;
}

static inline int is_10G(const struct adapter *adap)
{
	return adapter_info(adap)->caps & SUPPORTED_10000baseT_Full;
}

static inline int is_offload(const struct adapter *adap)
{
	return adap->params.offload;
}

static inline unsigned int core_ticks_per_usec(const struct adapter *adap)
{
	return adap->params.vpd.cclk / 1000;
}

static inline unsigned int is_pcie(const struct adapter *adap)
{
	return adap->params.pci.variant == PCI_VARIANT_PCIE;
}

void t3_set_reg_field(struct adapter *adap, unsigned int addr, u32 mask,
		      u32 val);
void t3_write_regs(struct adapter *adapter, const struct addr_val_pair *p,
		   int n, unsigned int offset);
int t3_wait_op_done_val(struct adapter *adapter, int reg, u32 mask,
			int polarity, int attempts, int delay, u32 *valp);
static inline int t3_wait_op_done(struct adapter *adapter, int reg, u32 mask,
				  int polarity, int attempts, int delay)
{
	return t3_wait_op_done_val(adapter, reg, mask, polarity, attempts,
				   delay, NULL);
}
int t3_mdio_change_bits(struct cphy *phy, int mmd, int reg, unsigned int clear,
			unsigned int set);
int t3_phy_reset(struct cphy *phy, int mmd, int wait);
int t3_phy_advertise(struct cphy *phy, unsigned int advert);
int t3_set_phy_speed_duplex(struct cphy *phy, int speed, int duplex);

void t3_intr_enable(struct adapter *adapter);
void t3_intr_disable(struct adapter *adapter);
void t3_intr_clear(struct adapter *adapter);
void t3_port_intr_enable(struct adapter *adapter, int idx);
void t3_port_intr_disable(struct adapter *adapter, int idx);
void t3_port_intr_clear(struct adapter *adapter, int idx);
int t3_slow_intr_handler(struct adapter *adapter);
int t3_phy_intr_handler(struct adapter *adapter);

void t3_link_changed(struct adapter *adapter, int port_id);
int t3_link_start(struct cphy *phy, struct cmac *mac, struct link_config *lc);
const struct adapter_info *t3_get_adapter_info(unsigned int board_id);
int t3_seeprom_read(struct adapter *adapter, u32 addr, u32 *data);
int t3_seeprom_write(struct adapter *adapter, u32 addr, u32 data);
int t3_seeprom_wp(struct adapter *adapter, int enable);
int t3_check_tpsram_version(struct adapter *adapter);
int t3_check_tpsram(struct adapter *adapter, u8 *tp_ram, unsigned int size);
int t3_set_proto_sram(struct adapter *adap, u8 *data);
int t3_read_flash(struct adapter *adapter, unsigned int addr,
		  unsigned int nwords, u32 *data, int byte_oriented);
int t3_load_fw(struct adapter *adapter, const u8 * fw_data, unsigned int size);
int t3_get_fw_version(struct adapter *adapter, u32 *vers);
int t3_check_fw_version(struct adapter *adapter);
int t3_init_hw(struct adapter *adapter, u32 fw_params);
void mac_prep(struct cmac *mac, struct adapter *adapter, int index);
void early_hw_init(struct adapter *adapter, const struct adapter_info *ai);
int t3_prep_adapter(struct adapter *adapter, const struct adapter_info *ai,
		    int reset);
void t3_led_ready(struct adapter *adapter);
void t3_fatal_err(struct adapter *adapter);
void t3_set_vlan_accel(struct adapter *adapter, unsigned int ports, int on);
void t3_config_rss(struct adapter *adapter, unsigned int rss_config,
		   const u8 * cpus, const u16 *rspq);
int t3_read_rss(struct adapter *adapter, u8 * lkup, u16 *map);
int t3_mps_set_active_ports(struct adapter *adap, unsigned int port_mask);
int t3_cim_ctl_blk_read(struct adapter *adap, unsigned int addr,
			unsigned int n, unsigned int *valp);
int t3_mc7_bd_read(struct mc7 *mc7, unsigned int start, unsigned int n,
		   u64 *buf);

int t3_mac_reset(struct cmac *mac);
void t3b_pcs_reset(struct cmac *mac);
int t3_mac_enable(struct cmac *mac, int which);
int t3_mac_disable(struct cmac *mac, int which);
int t3_mac_set_mtu(struct cmac *mac, unsigned int mtu);
int t3_mac_set_rx_mode(struct cmac *mac, struct t3_rx_mode *rm);
int t3_mac_set_address(struct cmac *mac, unsigned int idx, u8 addr[6]);
int t3_mac_set_num_ucast(struct cmac *mac, int n);
const struct mac_stats *t3_mac_update_stats(struct cmac *mac);
int t3_mac_set_speed_duplex_fc(struct cmac *mac, int speed, int duplex, int fc);
int t3b2_mac_watchdog_task(struct cmac *mac);

void t3_mc5_prep(struct adapter *adapter, struct mc5 *mc5, int mode);
int t3_mc5_init(struct mc5 *mc5, unsigned int nservers, unsigned int nfilters,
		unsigned int nroutes);
void t3_mc5_intr_handler(struct mc5 *mc5);
int t3_read_mc5_range(const struct mc5 *mc5, unsigned int start, unsigned int n,
		      u32 *buf);

int t3_tp_set_coalescing_size(struct adapter *adap, unsigned int size, int psh);
void t3_tp_set_max_rxsize(struct adapter *adap, unsigned int size);
void t3_tp_set_offload_mode(struct adapter *adap, int enable);
void t3_tp_get_mib_stats(struct adapter *adap, struct tp_mib_stats *tps);
void t3_load_mtus(struct adapter *adap, unsigned short mtus[NMTUS],
		  unsigned short alpha[NCCTRL_WIN],
		  unsigned short beta[NCCTRL_WIN], unsigned short mtu_cap);
void t3_read_hw_mtus(struct adapter *adap, unsigned short mtus[NMTUS]);
void t3_get_cong_cntl_tab(struct adapter *adap,
			  unsigned short incr[NMTUS][NCCTRL_WIN]);
void t3_config_trace_filter(struct adapter *adapter,
			    const struct trace_params *tp, int filter_index,
			    int invert, int enable);
int t3_config_sched(struct adapter *adap, unsigned int kbps, int sched);

void t3_sge_prep(struct adapter *adap, struct sge_params *p);
void t3_sge_init(struct adapter *adap, struct sge_params *p);
int t3_sge_init_ecntxt(struct adapter *adapter, unsigned int id, int gts_enable,
		       enum sge_context_type type, int respq, u64 base_addr,
		       unsigned int size, unsigned int token, int gen,
		       unsigned int cidx);
int t3_sge_init_flcntxt(struct adapter *adapter, unsigned int id,
			int gts_enable, u64 base_addr, unsigned int size,
			unsigned int esize, unsigned int cong_thres, int gen,
			unsigned int cidx);
int t3_sge_init_rspcntxt(struct adapter *adapter, unsigned int id,
			 int irq_vec_idx, u64 base_addr, unsigned int size,
			 unsigned int fl_thres, int gen, unsigned int cidx);
int t3_sge_init_cqcntxt(struct adapter *adapter, unsigned int id, u64 base_addr,
			unsigned int size, int rspq, int ovfl_mode,
			unsigned int credits, unsigned int credit_thres);
int t3_sge_enable_ecntxt(struct adapter *adapter, unsigned int id, int enable);
int t3_sge_disable_fl(struct adapter *adapter, unsigned int id);
int t3_sge_disable_rspcntxt(struct adapter *adapter, unsigned int id);
int t3_sge_disable_cqcntxt(struct adapter *adapter, unsigned int id);
int t3_sge_read_ecntxt(struct adapter *adapter, unsigned int id, u32 data[4]);
int t3_sge_read_fl(struct adapter *adapter, unsigned int id, u32 data[4]);
int t3_sge_read_cq(struct adapter *adapter, unsigned int id, u32 data[4]);
int t3_sge_read_rspq(struct adapter *adapter, unsigned int id, u32 data[4]);
int t3_sge_cqcntxt_op(struct adapter *adapter, unsigned int id, unsigned int op,
		      unsigned int credits);

void t3_vsc8211_phy_prep(struct cphy *phy, struct adapter *adapter,
			 int phy_addr, const struct mdio_ops *mdio_ops);
void t3_ael1002_phy_prep(struct cphy *phy, struct adapter *adapter,
			 int phy_addr, const struct mdio_ops *mdio_ops);
void t3_ael1006_phy_prep(struct cphy *phy, struct adapter *adapter,
			 int phy_addr, const struct mdio_ops *mdio_ops);
void t3_qt2045_phy_prep(struct cphy *phy, struct adapter *adapter, int phy_addr,
			const struct mdio_ops *mdio_ops);
void t3_xaui_direct_phy_prep(struct cphy *phy, struct adapter *adapter,
			     int phy_addr, const struct mdio_ops *mdio_ops);
#endif				/* __CHELSIO_COMMON_H */
