/*******************************************************************************

  Intel PRO/100 Linux driver
  Copyright(c) 1999 - 2006 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

/*
 *	e100.c: Intel(R) PRO/100 ethernet driver
 *
 *	(Re)written 2003 by scott.feldman@intel.com.  Based loosely on
 *	original e100 driver, but better described as a munging of
 *	e100, e1000, eepro100, tg3, 8139cp, and other drivers.
 *
 *	References:
 *		Intel 8255x 10/100 Mbps Ethernet Controller Family,
 *		Open Source Software Developers Manual,
 *		http://sourceforge.net/projects/e1000
 *
 *
 *	                      Theory of Operation
 *
 *	I.   General
 *
 *	The driver supports Intel(R) 10/100 Mbps PCI Fast Ethernet
 *	controller family, which includes the 82557, 82558, 82559, 82550,
 *	82551, and 82562 devices.  82558 and greater controllers
 *	integrate the Intel 82555 PHY.  The controllers are used in
 *	server and client network interface cards, as well as in
 *	LAN-On-Motherboard (LOM), CardBus, MiniPCI, and ICHx
 *	configurations.  8255x supports a 32-bit linear addressing
 *	mode and operates at 33Mhz PCI clock rate.
 *
 *	II.  Driver Operation
 *
 *	Memory-mapped mode is used exclusively to access the device's
 *	shared-memory structure, the Control/Status Registers (CSR). All
 *	setup, configuration, and control of the device, including queuing
 *	of Tx, Rx, and configuration commands is through the CSR.
 *	cmd_lock serializes accesses to the CSR command register.  cb_lock
 *	protects the shared Command Block List (CBL).
 *
 *	8255x is highly MII-compliant and all access to the PHY go
 *	through the Management Data Interface (MDI).  Consequently, the
 *	driver leverages the mii.c library shared with other MII-compliant
 *	devices.
 *
 *	Big- and Little-Endian byte order as well as 32- and 64-bit
 *	archs are supported.  Weak-ordered memory and non-cache-coherent
 *	archs are supported.
 *
 *	III. Transmit
 *
 *	A Tx skb is mapped and hangs off of a TCB.  TCBs are linked
 *	together in a fixed-size ring (CBL) thus forming the flexible mode
 *	memory structure.  A TCB marked with the suspend-bit indicates
 *	the end of the ring.  The last TCB processed suspends the
 *	controller, and the controller can be restarted by issue a CU
 *	resume command to continue from the suspend point, or a CU start
 *	command to start at a given position in the ring.
 *
 *	Non-Tx commands (config, multicast setup, etc) are linked
 *	into the CBL ring along with Tx commands.  The common structure
 *	used for both Tx and non-Tx commands is the Command Block (CB).
 *
 *	cb_to_use is the next CB to use for queuing a command; cb_to_clean
 *	is the next CB to check for completion; cb_to_send is the first
 *	CB to start on in case of a previous failure to resume.  CB clean
 *	up happens in interrupt context in response to a CU interrupt.
 *	cbs_avail keeps track of number of free CB resources available.
 *
 * 	Hardware padding of short packets to minimum packet size is
 * 	enabled.  82557 pads with 7Eh, while the later controllers pad
 * 	with 00h.
 *
 *	IV.  Receive
 *
 *	The Receive Frame Area (RFA) comprises a ring of Receive Frame
 *	Descriptors (RFD) + data buffer, thus forming the simplified mode
 *	memory structure.  Rx skbs are allocated to contain both the RFD
 *	and the data buffer, but the RFD is pulled off before the skb is
 *	indicated.  The data buffer is aligned such that encapsulated
 *	protocol headers are u32-aligned.  Since the RFD is part of the
 *	mapped shared memory, and completion status is contained within
 *	the RFD, the RFD must be dma_sync'ed to maintain a consistent
 *	view from software and hardware.
 *
 *	In order to keep updates to the RFD link field from colliding with
 *	hardware writes to mark packets complete, we use the feature that
 *	hardware will not write to a size 0 descriptor and mark the previous
 *	packet as end-of-list (EL).   After updating the link, we remove EL
 *	and only then restore the size such that hardware may use the
 *	previous-to-end RFD.
 *
 *	Under typical operation, the  receive unit (RU) is start once,
 *	and the controller happily fills RFDs as frames arrive.  If
 *	replacement RFDs cannot be allocated, or the RU goes non-active,
 *	the RU must be restarted.  Frame arrival generates an interrupt,
 *	and Rx indication and re-allocation happen in the same context,
 *	therefore no locking is required.  A software-generated interrupt
 *	is generated from the watchdog to recover from a failed allocation
 *	scenario where all Rx resources have been indicated and none re-
 *	placed.
 *
 *	V.   Miscellaneous
 *
 * 	VLAN offloading of tagging, stripping and filtering is not
 * 	supported, but driver will accommodate the extra 4-byte VLAN tag
 * 	for processing by upper layers.  Tx/Rx Checksum offloading is not
 * 	supported.  Tx Scatter/Gather is not supported.  Jumbo Frames is
 * 	not supported (hardware limitation).
 *
 * 	MagicPacket(tm) WoL support is enabled/disabled via ethtool.
 *
 * 	Thanks to JC (jchapman@katalix.com) for helping with
 * 	testing/troubleshooting the development driver.
 *
 * 	TODO:
 * 	o several entry points race with dev->close
 * 	o check for tx-no-resources/stop Q races with tx clean/wake Q
 *
 *	FIXES:
 * 2005/12/02 - Michael O'Donnell <Michael.ODonnell at stratus dot com>
 *	- Stratus87247: protect MDI control register manipulations
 * 2009/06/01 - Andreas Mohr <andi at lisas dot de>
 *      - add clean lowlevel I/O emulation for cards with MII-lacking PHYs
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/hardirq.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/string.h>
#include <linux/firmware.h>
#include <linux/rtnetlink.h>
#include <asm/unaligned.h>


#define DRV_NAME		"e100"
#define DRV_EXT			"-NAPI"
#define DRV_VERSION		"3.5.24-k2"DRV_EXT
#define DRV_DESCRIPTION		"Intel(R) PRO/100 Network Driver"
#define DRV_COPYRIGHT		"Copyright(c) 1999-2006 Intel Corporation"

#define E100_WATCHDOG_PERIOD	(2 * HZ)
#define E100_NAPI_WEIGHT	16

#define FIRMWARE_D101M		"e100/d101m_ucode.bin"
#define FIRMWARE_D101S		"e100/d101s_ucode.bin"
#define FIRMWARE_D102E		"e100/d102e_ucode.bin"

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR(DRV_COPYRIGHT);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_FIRMWARE(FIRMWARE_D101M);
MODULE_FIRMWARE(FIRMWARE_D101S);
MODULE_FIRMWARE(FIRMWARE_D102E);

static int debug = 3;
static int eeprom_bad_csum_allow = 0;
static int use_io = 0;
module_param(debug, int, 0);
module_param(eeprom_bad_csum_allow, int, 0);
module_param(use_io, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");
MODULE_PARM_DESC(eeprom_bad_csum_allow, "Allow bad eeprom checksums");
MODULE_PARM_DESC(use_io, "Force use of i/o access mode");

#define INTEL_8255X_ETHERNET_DEVICE(device_id, ich) {\
	PCI_VENDOR_ID_INTEL, device_id, PCI_ANY_ID, PCI_ANY_ID, \
	PCI_CLASS_NETWORK_ETHERNET << 8, 0xFFFF00, ich }
static DEFINE_PCI_DEVICE_TABLE(e100_id_table) = {
	INTEL_8255X_ETHERNET_DEVICE(0x1029, 0),
	INTEL_8255X_ETHERNET_DEVICE(0x1030, 0),
	INTEL_8255X_ETHERNET_DEVICE(0x1031, 3),
	INTEL_8255X_ETHERNET_DEVICE(0x1032, 3),
	INTEL_8255X_ETHERNET_DEVICE(0x1033, 3),
	INTEL_8255X_ETHERNET_DEVICE(0x1034, 3),
	INTEL_8255X_ETHERNET_DEVICE(0x1038, 3),
	INTEL_8255X_ETHERNET_DEVICE(0x1039, 4),
	INTEL_8255X_ETHERNET_DEVICE(0x103A, 4),
	INTEL_8255X_ETHERNET_DEVICE(0x103B, 4),
	INTEL_8255X_ETHERNET_DEVICE(0x103C, 4),
	INTEL_8255X_ETHERNET_DEVICE(0x103D, 4),
	INTEL_8255X_ETHERNET_DEVICE(0x103E, 4),
	INTEL_8255X_ETHERNET_DEVICE(0x1050, 5),
	INTEL_8255X_ETHERNET_DEVICE(0x1051, 5),
	INTEL_8255X_ETHERNET_DEVICE(0x1052, 5),
	INTEL_8255X_ETHERNET_DEVICE(0x1053, 5),
	INTEL_8255X_ETHERNET_DEVICE(0x1054, 5),
	INTEL_8255X_ETHERNET_DEVICE(0x1055, 5),
	INTEL_8255X_ETHERNET_DEVICE(0x1056, 5),
	INTEL_8255X_ETHERNET_DEVICE(0x1057, 5),
	INTEL_8255X_ETHERNET_DEVICE(0x1059, 0),
	INTEL_8255X_ETHERNET_DEVICE(0x1064, 6),
	INTEL_8255X_ETHERNET_DEVICE(0x1065, 6),
	INTEL_8255X_ETHERNET_DEVICE(0x1066, 6),
	INTEL_8255X_ETHERNET_DEVICE(0x1067, 6),
	INTEL_8255X_ETHERNET_DEVICE(0x1068, 6),
	INTEL_8255X_ETHERNET_DEVICE(0x1069, 6),
	INTEL_8255X_ETHERNET_DEVICE(0x106A, 6),
	INTEL_8255X_ETHERNET_DEVICE(0x106B, 6),
	INTEL_8255X_ETHERNET_DEVICE(0x1091, 7),
	INTEL_8255X_ETHERNET_DEVICE(0x1092, 7),
	INTEL_8255X_ETHERNET_DEVICE(0x1093, 7),
	INTEL_8255X_ETHERNET_DEVICE(0x1094, 7),
	INTEL_8255X_ETHERNET_DEVICE(0x1095, 7),
	INTEL_8255X_ETHERNET_DEVICE(0x10fe, 7),
	INTEL_8255X_ETHERNET_DEVICE(0x1209, 0),
	INTEL_8255X_ETHERNET_DEVICE(0x1229, 0),
	INTEL_8255X_ETHERNET_DEVICE(0x2449, 2),
	INTEL_8255X_ETHERNET_DEVICE(0x2459, 2),
	INTEL_8255X_ETHERNET_DEVICE(0x245D, 2),
	INTEL_8255X_ETHERNET_DEVICE(0x27DC, 7),
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, e100_id_table);

enum mac {
	mac_82557_D100_A  = 0,
	mac_82557_D100_B  = 1,
	mac_82557_D100_C  = 2,
	mac_82558_D101_A4 = 4,
	mac_82558_D101_B0 = 5,
	mac_82559_D101M   = 8,
	mac_82559_D101S   = 9,
	mac_82550_D102    = 12,
	mac_82550_D102_C  = 13,
	mac_82551_E       = 14,
	mac_82551_F       = 15,
	mac_82551_10      = 16,
	mac_unknown       = 0xFF,
};

enum phy {
	phy_100a     = 0x000003E0,
	phy_100c     = 0x035002A8,
	phy_82555_tx = 0x015002A8,
	phy_nsc_tx   = 0x5C002000,
	phy_82562_et = 0x033002A8,
	phy_82562_em = 0x032002A8,
	phy_82562_ek = 0x031002A8,
	phy_82562_eh = 0x017002A8,
	phy_82552_v  = 0xd061004d,
	phy_unknown  = 0xFFFFFFFF,
};

/* CSR (Control/Status Registers) */
struct csr {
	struct {
		u8 status;
		u8 stat_ack;
		u8 cmd_lo;
		u8 cmd_hi;
		u32 gen_ptr;
	} scb;
	u32 port;
	u16 flash_ctrl;
	u8 eeprom_ctrl_lo;
	u8 eeprom_ctrl_hi;
	u32 mdi_ctrl;
	u32 rx_dma_count;
};

enum scb_status {
	rus_no_res       = 0x08,
	rus_ready        = 0x10,
	rus_mask         = 0x3C,
};

enum ru_state  {
	RU_SUSPENDED = 0,
	RU_RUNNING	 = 1,
	RU_UNINITIALIZED = -1,
};

enum scb_stat_ack {
	stat_ack_not_ours    = 0x00,
	stat_ack_sw_gen      = 0x04,
	stat_ack_rnr         = 0x10,
	stat_ack_cu_idle     = 0x20,
	stat_ack_frame_rx    = 0x40,
	stat_ack_cu_cmd_done = 0x80,
	stat_ack_not_present = 0xFF,
	stat_ack_rx = (stat_ack_sw_gen | stat_ack_rnr | stat_ack_frame_rx),
	stat_ack_tx = (stat_ack_cu_idle | stat_ack_cu_cmd_done),
};

enum scb_cmd_hi {
	irq_mask_none = 0x00,
	irq_mask_all  = 0x01,
	irq_sw_gen    = 0x02,
};

enum scb_cmd_lo {
	cuc_nop        = 0x00,
	ruc_start      = 0x01,
	ruc_load_base  = 0x06,
	cuc_start      = 0x10,
	cuc_resume     = 0x20,
	cuc_dump_addr  = 0x40,
	cuc_dump_stats = 0x50,
	cuc_load_base  = 0x60,
	cuc_dump_reset = 0x70,
};

enum cuc_dump {
	cuc_dump_complete       = 0x0000A005,
	cuc_dump_reset_complete = 0x0000A007,
};

enum port {
	software_reset  = 0x0000,
	selftest        = 0x0001,
	selective_reset = 0x0002,
};

enum eeprom_ctrl_lo {
	eesk = 0x01,
	eecs = 0x02,
	eedi = 0x04,
	eedo = 0x08,
};

enum mdi_ctrl {
	mdi_write = 0x04000000,
	mdi_read  = 0x08000000,
	mdi_ready = 0x10000000,
};

enum eeprom_op {
	op_write = 0x05,
	op_read  = 0x06,
	op_ewds  = 0x10,
	op_ewen  = 0x13,
};

enum eeprom_offsets {
	eeprom_cnfg_mdix  = 0x03,
	eeprom_phy_iface  = 0x06,
	eeprom_id         = 0x0A,
	eeprom_config_asf = 0x0D,
	eeprom_smbus_addr = 0x90,
};

enum eeprom_cnfg_mdix {
	eeprom_mdix_enabled = 0x0080,
};

enum eeprom_phy_iface {
	NoSuchPhy = 0,
	I82553AB,
	I82553C,
	I82503,
	DP83840,
	S80C240,
	S80C24,
	I82555,
	DP83840A = 10,
};

enum eeprom_id {
	eeprom_id_wol = 0x0020,
};

enum eeprom_config_asf {
	eeprom_asf = 0x8000,
	eeprom_gcl = 0x4000,
};

enum cb_status {
	cb_complete = 0x8000,
	cb_ok       = 0x2000,
};

/**
 * cb_command - Command Block flags
 * @cb_tx_nc:  0: controler does CRC (normal),  1: CRC from skb memory
 */
enum cb_command {
	cb_nop    = 0x0000,
	cb_iaaddr = 0x0001,
	cb_config = 0x0002,
	cb_multi  = 0x0003,
	cb_tx     = 0x0004,
	cb_ucode  = 0x0005,
	cb_dump   = 0x0006,
	cb_tx_sf  = 0x0008,
	cb_tx_nc  = 0x0010,
	cb_cid    = 0x1f00,
	cb_i      = 0x2000,
	cb_s      = 0x4000,
	cb_el     = 0x8000,
};

struct rfd {
	__le16 status;
	__le16 command;
	__le32 link;
	__le32 rbd;
	__le16 actual_size;
	__le16 size;
};

struct rx {
	struct rx *next, *prev;
	struct sk_buff *skb;
	dma_addr_t dma_addr;
};

#if defined(__BIG_ENDIAN_BITFIELD)
#define X(a,b)	b,a
#else
#define X(a,b)	a,b
#endif
struct config {
/*0*/	u8 X(byte_count:6, pad0:2);
/*1*/	u8 X(X(rx_fifo_limit:4, tx_fifo_limit:3), pad1:1);
/*2*/	u8 adaptive_ifs;
/*3*/	u8 X(X(X(X(mwi_enable:1, type_enable:1), read_align_enable:1),
	   term_write_cache_line:1), pad3:4);
/*4*/	u8 X(rx_dma_max_count:7, pad4:1);
/*5*/	u8 X(tx_dma_max_count:7, dma_max_count_enable:1);
/*6*/	u8 X(X(X(X(X(X(X(late_scb_update:1, direct_rx_dma:1),
	   tno_intr:1), cna_intr:1), standard_tcb:1), standard_stat_counter:1),
	   rx_save_overruns : 1), rx_save_bad_frames : 1);
/*7*/	u8 X(X(X(X(X(rx_discard_short_frames:1, tx_underrun_retry:2),
	   pad7:2), rx_extended_rfd:1), tx_two_frames_in_fifo:1),
	   tx_dynamic_tbd:1);
/*8*/	u8 X(X(mii_mode:1, pad8:6), csma_disabled:1);
/*9*/	u8 X(X(X(X(X(rx_tcpudp_checksum:1, pad9:3), vlan_arp_tco:1),
	   link_status_wake:1), arp_wake:1), mcmatch_wake:1);
/*10*/	u8 X(X(X(pad10:3, no_source_addr_insertion:1), preamble_length:2),
	   loopback:2);
/*11*/	u8 X(linear_priority:3, pad11:5);
/*12*/	u8 X(X(linear_priority_mode:1, pad12:3), ifs:4);
/*13*/	u8 ip_addr_lo;
/*14*/	u8 ip_addr_hi;
/*15*/	u8 X(X(X(X(X(X(X(promiscuous_mode:1, broadcast_disabled:1),
	   wait_after_win:1), pad15_1:1), ignore_ul_bit:1), crc_16_bit:1),
	   pad15_2:1), crs_or_cdt:1);
/*16*/	u8 fc_delay_lo;
/*17*/	u8 fc_delay_hi;
/*18*/	u8 X(X(X(X(X(rx_stripping:1, tx_padding:1), rx_crc_transfer:1),
	   rx_long_ok:1), fc_priority_threshold:3), pad18:1);
/*19*/	u8 X(X(X(X(X(X(X(addr_wake:1, magic_packet_disable:1),
	   fc_disable:1), fc_restop:1), fc_restart:1), fc_reject:1),
	   full_duplex_force:1), full_duplex_pin:1);
/*20*/	u8 X(X(X(pad20_1:5, fc_priority_location:1), multi_ia:1), pad20_2:1);
/*21*/	u8 X(X(pad21_1:3, multicast_all:1), pad21_2:4);
/*22*/	u8 X(X(rx_d102_mode:1, rx_vlan_drop:1), pad22:6);
	u8 pad_d102[9];
};

#define E100_MAX_MULTICAST_ADDRS	64
struct multi {
	__le16 count;
	u8 addr[E100_MAX_MULTICAST_ADDRS * ETH_ALEN + 2/*pad*/];
};

/* Important: keep total struct u32-aligned */
#define UCODE_SIZE			134
struct cb {
	__le16 status;
	__le16 command;
	__le32 link;
	union {
		u8 iaaddr[ETH_ALEN];
		__le32 ucode[UCODE_SIZE];
		struct config config;
		struct multi multi;
		struct {
			u32 tbd_array;
			u16 tcb_byte_count;
			u8 threshold;
			u8 tbd_count;
			struct {
				__le32 buf_addr;
				__le16 size;
				u16 eol;
			} tbd;
		} tcb;
		__le32 dump_buffer_addr;
	} u;
	struct cb *next, *prev;
	dma_addr_t dma_addr;
	struct sk_buff *skb;
};

enum loopback {
	lb_none = 0, lb_mac = 1, lb_phy = 3,
};

struct stats {
	__le32 tx_good_frames, tx_max_collisions, tx_late_collisions,
		tx_underruns, tx_lost_crs, tx_deferred, tx_single_collisions,
		tx_multiple_collisions, tx_total_collisions;
	__le32 rx_good_frames, rx_crc_errors, rx_alignment_errors,
		rx_resource_errors, rx_overrun_errors, rx_cdt_errors,
		rx_short_frame_errors;
	__le32 fc_xmt_pause, fc_rcv_pause, fc_rcv_unsupported;
	__le16 xmt_tco_frames, rcv_tco_frames;
	__le32 complete;
};

struct mem {
	struct {
		u32 signature;
		u32 result;
	} selftest;
	struct stats stats;
	u8 dump_buf[596];
};

struct param_range {
	u32 min;
	u32 max;
	u32 count;
};

struct params {
	struct param_range rfds;
	struct param_range cbs;
};

struct nic {
	/* Begin: frequently used values: keep adjacent for cache effect */
	u32 msg_enable				____cacheline_aligned;
	struct net_device *netdev;
	struct pci_dev *pdev;
	u16 (*mdio_ctrl)(struct nic *nic, u32 addr, u32 dir, u32 reg, u16 data);

	struct rx *rxs				____cacheline_aligned;
	struct rx *rx_to_use;
	struct rx *rx_to_clean;
	struct rfd blank_rfd;
	enum ru_state ru_running;

	spinlock_t cb_lock			____cacheline_aligned;
	spinlock_t cmd_lock;
	struct csr __iomem *csr;
	enum scb_cmd_lo cuc_cmd;
	unsigned int cbs_avail;
	struct napi_struct napi;
	struct cb *cbs;
	struct cb *cb_to_use;
	struct cb *cb_to_send;
	struct cb *cb_to_clean;
	__le16 tx_command;
	/* End: frequently used values: keep adjacent for cache effect */

	enum {
		ich                = (1 << 0),
		promiscuous        = (1 << 1),
		multicast_all      = (1 << 2),
		wol_magic          = (1 << 3),
		ich_10h_workaround = (1 << 4),
	} flags					____cacheline_aligned;

	enum mac mac;
	enum phy phy;
	struct params params;
	struct timer_list watchdog;
	struct mii_if_info mii;
	struct work_struct tx_timeout_task;
	enum loopback loopback;

	struct mem *mem;
	dma_addr_t dma_addr;

	struct pci_pool *cbs_pool;
	dma_addr_t cbs_dma_addr;
	u8 adaptive_ifs;
	u8 tx_threshold;
	u32 tx_frames;
	u32 tx_collisions;
	u32 tx_deferred;
	u32 tx_single_collisions;
	u32 tx_multiple_collisions;
	u32 tx_fc_pause;
	u32 tx_tco_frames;

	u32 rx_fc_pause;
	u32 rx_fc_unsupported;
	u32 rx_tco_frames;
	u32 rx_short_frame_errors;
	u32 rx_over_length_errors;

	u16 eeprom_wc;
	__le16 eeprom[256];
	spinlock_t mdio_lock;
	const struct firmware *fw;
};

static inline void e100_write_flush(struct nic *nic)
{
	/* Flush previous PCI writes through intermediate bridges
	 * by doing a benign read */
	(void)ioread8(&nic->csr->scb.status);
}

static void e100_enable_irq(struct nic *nic)
{
	unsigned long flags;

	spin_lock_irqsave(&nic->cmd_lock, flags);
	iowrite8(irq_mask_none, &nic->csr->scb.cmd_hi);
	e100_write_flush(nic);
	spin_unlock_irqrestore(&nic->cmd_lock, flags);
}

static void e100_disable_irq(struct nic *nic)
{
	unsigned long flags;

	spin_lock_irqsave(&nic->cmd_lock, flags);
	iowrite8(irq_mask_all, &nic->csr->scb.cmd_hi);
	e100_write_flush(nic);
	spin_unlock_irqrestore(&nic->cmd_lock, flags);
}

static void e100_hw_reset(struct nic *nic)
{
	/* Put CU and RU into idle with a selective reset to get
	 * device off of PCI bus */
	iowrite32(selective_reset, &nic->csr->port);
	e100_write_flush(nic); udelay(20);

	/* Now fully reset device */
	iowrite32(software_reset, &nic->csr->port);
	e100_write_flush(nic); udelay(20);

	/* Mask off our interrupt line - it's unmasked after reset */
	e100_disable_irq(nic);
}

static int e100_self_test(struct nic *nic)
{
	u32 dma_addr = nic->dma_addr + offsetof(struct mem, selftest);

	/* Passing the self-test is a pretty good indication
	 * that the device can DMA to/from host memory */

	nic->mem->selftest.signature = 0;
	nic->mem->selftest.result = 0xFFFFFFFF;

	iowrite32(selftest | dma_addr, &nic->csr->port);
	e100_write_flush(nic);
	/* Wait 10 msec for self-test to complete */
	msleep(10);

	/* Interrupts are enabled after self-test */
	e100_disable_irq(nic);

	/* Check results of self-test */
	if (nic->mem->selftest.result != 0) {
		netif_err(nic, hw, nic->netdev,
			  "Self-test failed: result=0x%08X\n",
			  nic->mem->selftest.result);
		return -ETIMEDOUT;
	}
	if (nic->mem->selftest.signature == 0) {
		netif_err(nic, hw, nic->netdev, "Self-test failed: timed out\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void e100_eeprom_write(struct nic *nic, u16 addr_len, u16 addr, __le16 data)
{
	u32 cmd_addr_data[3];
	u8 ctrl;
	int i, j;

	/* Three cmds: write/erase enable, write data, write/erase disable */
	cmd_addr_data[0] = op_ewen << (addr_len - 2);
	cmd_addr_data[1] = (((op_write << addr_len) | addr) << 16) |
		le16_to_cpu(data);
	cmd_addr_data[2] = op_ewds << (addr_len - 2);

	/* Bit-bang cmds to write word to eeprom */
	for (j = 0; j < 3; j++) {

		/* Chip select */
		iowrite8(eecs | eesk, &nic->csr->eeprom_ctrl_lo);
		e100_write_flush(nic); udelay(4);

		for (i = 31; i >= 0; i--) {
			ctrl = (cmd_addr_data[j] & (1 << i)) ?
				eecs | eedi : eecs;
			iowrite8(ctrl, &nic->csr->eeprom_ctrl_lo);
			e100_write_flush(nic); udelay(4);

			iowrite8(ctrl | eesk, &nic->csr->eeprom_ctrl_lo);
			e100_write_flush(nic); udelay(4);
		}
		/* Wait 10 msec for cmd to complete */
		msleep(10);

		/* Chip deselect */
		iowrite8(0, &nic->csr->eeprom_ctrl_lo);
		e100_write_flush(nic); udelay(4);
	}
};

/* General technique stolen from the eepro100 driver - very clever */
static __le16 e100_eeprom_read(struct nic *nic, u16 *addr_len, u16 addr)
{
	u32 cmd_addr_data;
	u16 data = 0;
	u8 ctrl;
	int i;

	cmd_addr_data = ((op_read << *addr_len) | addr) << 16;

	/* Chip select */
	iowrite8(eecs | eesk, &nic->csr->eeprom_ctrl_lo);
	e100_write_flush(nic); udelay(4);

	/* Bit-bang to read word from eeprom */
	for (i = 31; i >= 0; i--) {
		ctrl = (cmd_addr_data & (1 << i)) ? eecs | eedi : eecs;
		iowrite8(ctrl, &nic->csr->eeprom_ctrl_lo);
		e100_write_flush(nic); udelay(4);

		iowrite8(ctrl | eesk, &nic->csr->eeprom_ctrl_lo);
		e100_write_flush(nic); udelay(4);

		/* Eeprom drives a dummy zero to EEDO after receiving
		 * complete address.  Use this to adjust addr_len. */
		ctrl = ioread8(&nic->csr->eeprom_ctrl_lo);
		if (!(ctrl & eedo) && i > 16) {
			*addr_len -= (i - 16);
			i = 17;
		}

		data = (data << 1) | (ctrl & eedo ? 1 : 0);
	}

	/* Chip deselect */
	iowrite8(0, &nic->csr->eeprom_ctrl_lo);
	e100_write_flush(nic); udelay(4);

	return cpu_to_le16(data);
};

/* Load entire EEPROM image into driver cache and validate checksum */
static int e100_eeprom_load(struct nic *nic)
{
	u16 addr, addr_len = 8, checksum = 0;

	/* Try reading with an 8-bit addr len to discover actual addr len */
	e100_eeprom_read(nic, &addr_len, 0);
	nic->eeprom_wc = 1 << addr_len;

	for (addr = 0; addr < nic->eeprom_wc; addr++) {
		nic->eeprom[addr] = e100_eeprom_read(nic, &addr_len, addr);
		if (addr < nic->eeprom_wc - 1)
			checksum += le16_to_cpu(nic->eeprom[addr]);
	}

	/* The checksum, stored in the last word, is calculated such that
	 * the sum of words should be 0xBABA */
	if (cpu_to_le16(0xBABA - checksum) != nic->eeprom[nic->eeprom_wc - 1]) {
		netif_err(nic, probe, nic->netdev, "EEPROM corrupted\n");
		if (!eeprom_bad_csum_allow)
			return -EAGAIN;
	}

	return 0;
}

/* Save (portion of) driver EEPROM cache to device and update checksum */
static int e100_eeprom_save(struct nic *nic, u16 start, u16 count)
{
	u16 addr, addr_len = 8, checksum = 0;

	/* Try reading with an 8-bit addr len to discover actual addr len */
	e100_eeprom_read(nic, &addr_len, 0);
	nic->eeprom_wc = 1 << addr_len;

	if (start + count >= nic->eeprom_wc)
		return -EINVAL;

	for (addr = start; addr < start + count; addr++)
		e100_eeprom_write(nic, addr_len, addr, nic->eeprom[addr]);

	/* The checksum, stored in the last word, is calculated such that
	 * the sum of words should be 0xBABA */
	for (addr = 0; addr < nic->eeprom_wc - 1; addr++)
		checksum += le16_to_cpu(nic->eeprom[addr]);
	nic->eeprom[nic->eeprom_wc - 1] = cpu_to_le16(0xBABA - checksum);
	e100_eeprom_write(nic, addr_len, nic->eeprom_wc - 1,
		nic->eeprom[nic->eeprom_wc - 1]);

	return 0;
}

#define E100_WAIT_SCB_TIMEOUT 20000 /* we might have to wait 100ms!!! */
#define E100_WAIT_SCB_FAST 20       /* delay like the old code */
static int e100_exec_cmd(struct nic *nic, u8 cmd, dma_addr_t dma_addr)
{
	unsigned long flags;
	unsigned int i;
	int err = 0;

	spin_lock_irqsave(&nic->cmd_lock, flags);

	/* Previous command is accepted when SCB clears */
	for (i = 0; i < E100_WAIT_SCB_TIMEOUT; i++) {
		if (likely(!ioread8(&nic->csr->scb.cmd_lo)))
			break;
		cpu_relax();
		if (unlikely(i > E100_WAIT_SCB_FAST))
			udelay(5);
	}
	if (unlikely(i == E100_WAIT_SCB_TIMEOUT)) {
		err = -EAGAIN;
		goto err_unlock;
	}

	if (unlikely(cmd != cuc_resume))
		iowrite32(dma_addr, &nic->csr->scb.gen_ptr);
	iowrite8(cmd, &nic->csr->scb.cmd_lo);

err_unlock:
	spin_unlock_irqrestore(&nic->cmd_lock, flags);

	return err;
}

static int e100_exec_cb(struct nic *nic, struct sk_buff *skb,
	int (*cb_prepare)(struct nic *, struct cb *, struct sk_buff *))
{
	struct cb *cb;
	unsigned long flags;
	int err = 0;

	spin_lock_irqsave(&nic->cb_lock, flags);

	if (unlikely(!nic->cbs_avail)) {
		err = -ENOMEM;
		goto err_unlock;
	}

	cb = nic->cb_to_use;
	nic->cb_to_use = cb->next;
	nic->cbs_avail--;
	cb->skb = skb;

	err = cb_prepare(nic, cb, skb);
	if (err)
		goto err_unlock;

	if (unlikely(!nic->cbs_avail))
		err = -ENOSPC;


	/* Order is important otherwise we'll be in a race with h/w:
	 * set S-bit in current first, then clear S-bit in previous. */
	cb->command |= cpu_to_le16(cb_s);
	wmb();
	cb->prev->command &= cpu_to_le16(~cb_s);

	while (nic->cb_to_send != nic->cb_to_use) {
		if (unlikely(e100_exec_cmd(nic, nic->cuc_cmd,
			nic->cb_to_send->dma_addr))) {
			/* Ok, here's where things get sticky.  It's
			 * possible that we can't schedule the command
			 * because the controller is too busy, so
			 * let's just queue the command and try again
			 * when another command is scheduled. */
			if (err == -ENOSPC) {
				//request a reset
				schedule_work(&nic->tx_timeout_task);
			}
			break;
		} else {
			nic->cuc_cmd = cuc_resume;
			nic->cb_to_send = nic->cb_to_send->next;
		}
	}

err_unlock:
	spin_unlock_irqrestore(&nic->cb_lock, flags);

	return err;
}

static int mdio_read(struct net_device *netdev, int addr, int reg)
{
	struct nic *nic = netdev_priv(netdev);
	return nic->mdio_ctrl(nic, addr, mdi_read, reg, 0);
}

static void mdio_write(struct net_device *netdev, int addr, int reg, int data)
{
	struct nic *nic = netdev_priv(netdev);

	nic->mdio_ctrl(nic, addr, mdi_write, reg, data);
}

/* the standard mdio_ctrl() function for usual MII-compliant hardware */
static u16 mdio_ctrl_hw(struct nic *nic, u32 addr, u32 dir, u32 reg, u16 data)
{
	u32 data_out = 0;
	unsigned int i;
	unsigned long flags;


	/*
	 * Stratus87247: we shouldn't be writing the MDI control
	 * register until the Ready bit shows True.  Also, since
	 * manipulation of the MDI control registers is a multi-step
	 * procedure it should be done under lock.
	 */
	spin_lock_irqsave(&nic->mdio_lock, flags);
	for (i = 100; i; --i) {
		if (ioread32(&nic->csr->mdi_ctrl) & mdi_ready)
			break;
		udelay(20);
	}
	if (unlikely(!i)) {
		netdev_err(nic->netdev, "e100.mdio_ctrl won't go Ready\n");
		spin_unlock_irqrestore(&nic->mdio_lock, flags);
		return 0;		/* No way to indicate timeout error */
	}
	iowrite32((reg << 16) | (addr << 21) | dir | data, &nic->csr->mdi_ctrl);

	for (i = 0; i < 100; i++) {
		udelay(20);
		if ((data_out = ioread32(&nic->csr->mdi_ctrl)) & mdi_ready)
			break;
	}
	spin_unlock_irqrestore(&nic->mdio_lock, flags);
	netif_printk(nic, hw, KERN_DEBUG, nic->netdev,
		     "%s:addr=%d, reg=%d, data_in=0x%04X, data_out=0x%04X\n",
		     dir == mdi_read ? "READ" : "WRITE",
		     addr, reg, data, data_out);
	return (u16)data_out;
}

/* slightly tweaked mdio_ctrl() function for phy_82552_v specifics */
static u16 mdio_ctrl_phy_82552_v(struct nic *nic,
				 u32 addr,
				 u32 dir,
				 u32 reg,
				 u16 data)
{
	if ((reg == MII_BMCR) && (dir == mdi_write)) {
		if (data & (BMCR_ANRESTART | BMCR_ANENABLE)) {
			u16 advert = mdio_read(nic->netdev, nic->mii.phy_id,
							MII_ADVERTISE);

			/*
			 * Workaround Si issue where sometimes the part will not
			 * autoneg to 100Mbps even when advertised.
			 */
			if (advert & ADVERTISE_100FULL)
				data |= BMCR_SPEED100 | BMCR_FULLDPLX;
			else if (advert & ADVERTISE_100HALF)
				data |= BMCR_SPEED100;
		}
	}
	return mdio_ctrl_hw(nic, addr, dir, reg, data);
}

/* Fully software-emulated mdio_ctrl() function for cards without
 * MII-compliant PHYs.
 * For now, this is mainly geared towards 80c24 support; in case of further
 * requirements for other types (i82503, ...?) either extend this mechanism
 * or split it, whichever is cleaner.
 */
static u16 mdio_ctrl_phy_mii_emulated(struct nic *nic,
				      u32 addr,
				      u32 dir,
				      u32 reg,
				      u16 data)
{
	/* might need to allocate a netdev_priv'ed register array eventually
	 * to be able to record state changes, but for now
	 * some fully hardcoded register handling ought to be ok I guess. */

	if (dir == mdi_read) {
		switch (reg) {
		case MII_BMCR:
			/* Auto-negotiation, right? */
			return  BMCR_ANENABLE |
				BMCR_FULLDPLX;
		case MII_BMSR:
			return	BMSR_LSTATUS /* for mii_link_ok() */ |
				BMSR_ANEGCAPABLE |
				BMSR_10FULL;
		case MII_ADVERTISE:
			/* 80c24 is a "combo card" PHY, right? */
			return	ADVERTISE_10HALF |
				ADVERTISE_10FULL;
		default:
			netif_printk(nic, hw, KERN_DEBUG, nic->netdev,
				     "%s:addr=%d, reg=%d, data=0x%04X: unimplemented emulation!\n",
				     dir == mdi_read ? "READ" : "WRITE",
				     addr, reg, data);
			return 0xFFFF;
		}
	} else {
		switch (reg) {
		default:
			netif_printk(nic, hw, KERN_DEBUG, nic->netdev,
				     "%s:addr=%d, reg=%d, data=0x%04X: unimplemented emulation!\n",
				     dir == mdi_read ? "READ" : "WRITE",
				     addr, reg, data);
			return 0xFFFF;
		}
	}
}
static inline int e100_phy_supports_mii(struct nic *nic)
{
	/* for now, just check it by comparing whether we
	   are using MII software emulation.
	*/
	return (nic->mdio_ctrl != mdio_ctrl_phy_mii_emulated);
}

static void e100_get_defaults(struct nic *nic)
{
	struct param_range rfds = { .min = 16, .max = 256, .count = 256 };
	struct param_range cbs  = { .min = 64, .max = 256, .count = 128 };

	/* MAC type is encoded as rev ID; exception: ICH is treated as 82559 */
	nic->mac = (nic->flags & ich) ? mac_82559_D101M : nic->pdev->revision;
	if (nic->mac == mac_unknown)
		nic->mac = mac_82557_D100_A;

	nic->params.rfds = rfds;
	nic->params.cbs = cbs;

	/* Quadwords to DMA into FIFO before starting frame transmit */
	nic->tx_threshold = 0xE0;

	/* no interrupt for every tx completion, delay = 256us if not 557 */
	nic->tx_command = cpu_to_le16(cb_tx | cb_tx_sf |
		((nic->mac >= mac_82558_D101_A4) ? cb_cid : cb_i));

	/* Template for a freshly allocated RFD */
	nic->blank_rfd.command = 0;
	nic->blank_rfd.rbd = cpu_to_le32(0xFFFFFFFF);
	nic->blank_rfd.size = cpu_to_le16(VLAN_ETH_FRAME_LEN + ETH_FCS_LEN);

	/* MII setup */
	nic->mii.phy_id_mask = 0x1F;
	nic->mii.reg_num_mask = 0x1F;
	nic->mii.dev = nic->netdev;
	nic->mii.mdio_read = mdio_read;
	nic->mii.mdio_write = mdio_write;
}

static int e100_configure(struct nic *nic, struct cb *cb, struct sk_buff *skb)
{
	struct config *config = &cb->u.config;
	u8 *c = (u8 *)config;
	struct net_device *netdev = nic->netdev;

	cb->command = cpu_to_le16(cb_config);

	memset(config, 0, sizeof(struct config));

	config->byte_count = 0x16;		/* bytes in this struct */
	config->rx_fifo_limit = 0x8;		/* bytes in FIFO before DMA */
	config->direct_rx_dma = 0x1;		/* reserved */
	config->standard_tcb = 0x1;		/* 1=standard, 0=extended */
	config->standard_stat_counter = 0x1;	/* 1=standard, 0=extended */
	config->rx_discard_short_frames = 0x1;	/* 1=discard, 0=pass */
	config->tx_underrun_retry = 0x3;	/* # of underrun retries */
	if (e100_phy_supports_mii(nic))
		config->mii_mode = 1;           /* 1=MII mode, 0=i82503 mode */
	config->pad10 = 0x6;
	config->no_source_addr_insertion = 0x1;	/* 1=no, 0=yes */
	config->preamble_length = 0x2;		/* 0=1, 1=3, 2=7, 3=15 bytes */
	config->ifs = 0x6;			/* x16 = inter frame spacing */
	config->ip_addr_hi = 0xF2;		/* ARP IP filter - not used */
	config->pad15_1 = 0x1;
	config->pad15_2 = 0x1;
	config->crs_or_cdt = 0x0;		/* 0=CRS only, 1=CRS or CDT */
	config->fc_delay_hi = 0x40;		/* time delay for fc frame */
	config->tx_padding = 0x1;		/* 1=pad short frames */
	config->fc_priority_threshold = 0x7;	/* 7=priority fc disabled */
	config->pad18 = 0x1;
	config->full_duplex_pin = 0x1;		/* 1=examine FDX# pin */
	config->pad20_1 = 0x1F;
	config->fc_priority_location = 0x1;	/* 1=byte#31, 0=byte#19 */
	config->pad21_1 = 0x5;

	config->adaptive_ifs = nic->adaptive_ifs;
	config->loopback = nic->loopback;

	if (nic->mii.force_media && nic->mii.full_duplex)
		config->full_duplex_force = 0x1;	/* 1=force, 0=auto */

	if (nic->flags & promiscuous || nic->loopback) {
		config->rx_save_bad_frames = 0x1;	/* 1=save, 0=discard */
		config->rx_discard_short_frames = 0x0;	/* 1=discard, 0=save */
		config->promiscuous_mode = 0x1;		/* 1=on, 0=off */
	}

	if (unlikely(netdev->features & NETIF_F_RXFCS))
		config->rx_crc_transfer = 0x1;	/* 1=save, 0=discard */

	if (nic->flags & multicast_all)
		config->multicast_all = 0x1;		/* 1=accept, 0=no */

	/* disable WoL when up */
	if (netif_running(nic->netdev) || !(nic->flags & wol_magic))
		config->magic_packet_disable = 0x1;	/* 1=off, 0=on */

	if (nic->mac >= mac_82558_D101_A4) {
		config->fc_disable = 0x1;	/* 1=Tx fc off, 0=Tx fc on */
		config->mwi_enable = 0x1;	/* 1=enable, 0=disable */
		config->standard_tcb = 0x0;	/* 1=standard, 0=extended */
		config->rx_long_ok = 0x1;	/* 1=VLANs ok, 0=standard */
		if (nic->mac >= mac_82559_D101M) {
			config->tno_intr = 0x1;		/* TCO stats enable */
			/* Enable TCO in extended config */
			if (nic->mac >= mac_82551_10) {
				config->byte_count = 0x20; /* extended bytes */
				config->rx_d102_mode = 0x1; /* GMRC for TCO */
			}
		} else {
			config->standard_stat_counter = 0x0;
		}
	}

	if (netdev->features & NETIF_F_RXALL) {
		config->rx_save_overruns = 0x1; /* 1=save, 0=discard */
		config->rx_save_bad_frames = 0x1;       /* 1=save, 0=discard */
		config->rx_discard_short_frames = 0x0;  /* 1=discard, 0=save */
	}

	netif_printk(nic, hw, KERN_DEBUG, nic->netdev, "[00-07]=%8ph\n",
		     c + 0);
	netif_printk(nic, hw, KERN_DEBUG, nic->netdev, "[08-15]=%8ph\n",
		     c + 8);
	netif_printk(nic, hw, KERN_DEBUG, nic->netdev, "[16-23]=%8ph\n",
		     c + 16);
	return 0;
}

/*************************************************************************
*  CPUSaver parameters
*
*  All CPUSaver parameters are 16-bit literals that are part of a
*  "move immediate value" instruction.  By changing the value of
*  the literal in the instruction before the code is loaded, the
*  driver can change the algorithm.
*
*  INTDELAY - This loads the dead-man timer with its initial value.
*    When this timer expires the interrupt is asserted, and the
*    timer is reset each time a new packet is received.  (see
*    BUNDLEMAX below to set the limit on number of chained packets)
*    The current default is 0x600 or 1536.  Experiments show that
*    the value should probably stay within the 0x200 - 0x1000.
*
*  BUNDLEMAX -
*    This sets the maximum number of frames that will be bundled.  In
*    some situations, such as the TCP windowing algorithm, it may be
*    better to limit the growth of the bundle size than let it go as
*    high as it can, because that could cause too much added latency.
*    The default is six, because this is the number of packets in the
*    default TCP window size.  A value of 1 would make CPUSaver indicate
*    an interrupt for every frame received.  If you do not want to put
*    a limit on the bundle size, set this value to xFFFF.
*
*  BUNDLESMALL -
*    This contains a bit-mask describing the minimum size frame that
*    will be bundled.  The default masks the lower 7 bits, which means
*    that any frame less than 128 bytes in length will not be bundled,
*    but will instead immediately generate an interrupt.  This does
*    not affect the current bundle in any way.  Any frame that is 128
*    bytes or large will be bundled normally.  This feature is meant
*    to provide immediate indication of ACK frames in a TCP environment.
*    Customers were seeing poor performance when a machine with CPUSaver
*    enabled was sending but not receiving.  The delay introduced when
*    the ACKs were received was enough to reduce total throughput, because
*    the sender would sit idle until the ACK was finally seen.
*
*    The current default is 0xFF80, which masks out the lower 7 bits.
*    This means that any frame which is x7F (127) bytes or smaller
*    will cause an immediate interrupt.  Because this value must be a
*    bit mask, there are only a few valid values that can be used.  To
*    turn this feature off, the driver can write the value xFFFF to the
*    lower word of this instruction (in the same way that the other
*    parameters are used).  Likewise, a value of 0xF800 (2047) would
*    cause an interrupt to be generated for every frame, because all
*    standard Ethernet frames are <= 2047 bytes in length.
*************************************************************************/

/* if you wish to disable the ucode functionality, while maintaining the
 * workarounds it provides, set the following defines to:
 * BUNDLESMALL 0
 * BUNDLEMAX 1
 * INTDELAY 1
 */
#define BUNDLESMALL 1
#define BUNDLEMAX (u16)6
#define INTDELAY (u16)1536 /* 0x600 */

/* Initialize firmware */
static const struct firmware *e100_request_firmware(struct nic *nic)
{
	const char *fw_name;
	const struct firmware *fw = nic->fw;
	u8 timer, bundle, min_size;
	int err = 0;
	bool required = false;

	/* do not load u-code for ICH devices */
	if (nic->flags & ich)
		return NULL;

	/* Search for ucode match against h/w revision
	 *
	 * Based on comments in the source code for the FreeBSD fxp
	 * driver, the FIRMWARE_D102E ucode includes both CPUSaver and
	 *
	 *    "fixes for bugs in the B-step hardware (specifically, bugs
	 *     with Inline Receive)."
	 *
	 * So we must fail if it cannot be loaded.
	 *
	 * The other microcode files are only required for the optional
	 * CPUSaver feature.  Nice to have, but no reason to fail.
	 */
	if (nic->mac == mac_82559_D101M) {
		fw_name = FIRMWARE_D101M;
	} else if (nic->mac == mac_82559_D101S) {
		fw_name = FIRMWARE_D101S;
	} else if (nic->mac == mac_82551_F || nic->mac == mac_82551_10) {
		fw_name = FIRMWARE_D102E;
		required = true;
	} else { /* No ucode on other devices */
		return NULL;
	}

	/* If the firmware has not previously been loaded, request a pointer
	 * to it. If it was previously loaded, we are reinitializing the
	 * adapter, possibly in a resume from hibernate, in which case
	 * request_firmware() cannot be used.
	 */
	if (!fw)
		err = request_firmware(&fw, fw_name, &nic->pdev->dev);

	if (err) {
		if (required) {
			netif_err(nic, probe, nic->netdev,
				  "Failed to load firmware \"%s\": %d\n",
				  fw_name, err);
			return ERR_PTR(err);
		} else {
			netif_info(nic, probe, nic->netdev,
				   "CPUSaver disabled. Needs \"%s\": %d\n",
				   fw_name, err);
			return NULL;
		}
	}

	/* Firmware should be precisely UCODE_SIZE (words) plus three bytes
	   indicating the offsets for BUNDLESMALL, BUNDLEMAX, INTDELAY */
	if (fw->size != UCODE_SIZE * 4 + 3) {
		netif_err(nic, probe, nic->netdev,
			  "Firmware \"%s\" has wrong size %zu\n",
			  fw_name, fw->size);
		release_firmware(fw);
		return ERR_PTR(-EINVAL);
	}

	/* Read timer, bundle and min_size from end of firmware blob */
	timer = fw->data[UCODE_SIZE * 4];
	bundle = fw->data[UCODE_SIZE * 4 + 1];
	min_size = fw->data[UCODE_SIZE * 4 + 2];

	if (timer >= UCODE_SIZE || bundle >= UCODE_SIZE ||
	    min_size >= UCODE_SIZE) {
		netif_err(nic, probe, nic->netdev,
			  "\"%s\" has bogus offset values (0x%x,0x%x,0x%x)\n",
			  fw_name, timer, bundle, min_size);
		release_firmware(fw);
		return ERR_PTR(-EINVAL);
	}

	/* OK, firmware is validated and ready to use. Save a pointer
	 * to it in the nic */
	nic->fw = fw;
	return fw;
}

static int e100_setup_ucode(struct nic *nic, struct cb *cb,
			     struct sk_buff *skb)
{
	const struct firmware *fw = (void *)skb;
	u8 timer, bundle, min_size;

	/* It's not a real skb; we just abused the fact that e100_exec_cb
	   will pass it through to here... */
	cb->skb = NULL;

	/* firmware is stored as little endian already */
	memcpy(cb->u.ucode, fw->data, UCODE_SIZE * 4);

	/* Read timer, bundle and min_size from end of firmware blob */
	timer = fw->data[UCODE_SIZE * 4];
	bundle = fw->data[UCODE_SIZE * 4 + 1];
	min_size = fw->data[UCODE_SIZE * 4 + 2];

	/* Insert user-tunable settings in cb->u.ucode */
	cb->u.ucode[timer] &= cpu_to_le32(0xFFFF0000);
	cb->u.ucode[timer] |= cpu_to_le32(INTDELAY);
	cb->u.ucode[bundle] &= cpu_to_le32(0xFFFF0000);
	cb->u.ucode[bundle] |= cpu_to_le32(BUNDLEMAX);
	cb->u.ucode[min_size] &= cpu_to_le32(0xFFFF0000);
	cb->u.ucode[min_size] |= cpu_to_le32((BUNDLESMALL) ? 0xFFFF : 0xFF80);

	cb->command = cpu_to_le16(cb_ucode | cb_el);
	return 0;
}

static inline int e100_load_ucode_wait(struct nic *nic)
{
	const struct firmware *fw;
	int err = 0, counter = 50;
	struct cb *cb = nic->cb_to_clean;

	fw = e100_request_firmware(nic);
	/* If it's NULL, then no ucode is required */
	if (!fw || IS_ERR(fw))
		return PTR_ERR(fw);

	if ((err = e100_exec_cb(nic, (void *)fw, e100_setup_ucode)))
		netif_err(nic, probe, nic->netdev,
			  "ucode cmd failed with error %d\n", err);

	/* must restart cuc */
	nic->cuc_cmd = cuc_start;

	/* wait for completion */
	e100_write_flush(nic);
	udelay(10);

	/* wait for possibly (ouch) 500ms */
	while (!(cb->status & cpu_to_le16(cb_complete))) {
		msleep(10);
		if (!--counter) break;
	}

	/* ack any interrupts, something could have been set */
	iowrite8(~0, &nic->csr->scb.stat_ack);

	/* if the command failed, or is not OK, notify and return */
	if (!counter || !(cb->status & cpu_to_le16(cb_ok))) {
		netif_err(nic, probe, nic->netdev, "ucode load failed\n");
		err = -EPERM;
	}

	return err;
}

static int e100_setup_iaaddr(struct nic *nic, struct cb *cb,
	struct sk_buff *skb)
{
	cb->command = cpu_to_le16(cb_iaaddr);
	memcpy(cb->u.iaaddr, nic->netdev->dev_addr, ETH_ALEN);
	return 0;
}

static int e100_dump(struct nic *nic, struct cb *cb, struct sk_buff *skb)
{
	cb->command = cpu_to_le16(cb_dump);
	cb->u.dump_buffer_addr = cpu_to_le32(nic->dma_addr +
		offsetof(struct mem, dump_buf));
	return 0;
}

static int e100_phy_check_without_mii(struct nic *nic)
{
	u8 phy_type;
	int without_mii;

	phy_type = (nic->eeprom[eeprom_phy_iface] >> 8) & 0x0f;

	switch (phy_type) {
	case NoSuchPhy: /* Non-MII PHY; UNTESTED! */
	case I82503: /* Non-MII PHY; UNTESTED! */
	case S80C24: /* Non-MII PHY; tested and working */
		/* paragraph from the FreeBSD driver, "FXP_PHY_80C24":
		 * The Seeq 80c24 AutoDUPLEX(tm) Ethernet Interface Adapter
		 * doesn't have a programming interface of any sort.  The
		 * media is sensed automatically based on how the link partner
		 * is configured.  This is, in essence, manual configuration.
		 */
		netif_info(nic, probe, nic->netdev,
			   "found MII-less i82503 or 80c24 or other PHY\n");

		nic->mdio_ctrl = mdio_ctrl_phy_mii_emulated;
		nic->mii.phy_id = 0; /* is this ok for an MII-less PHY? */

		/* these might be needed for certain MII-less cards...
		 * nic->flags |= ich;
		 * nic->flags |= ich_10h_workaround; */

		without_mii = 1;
		break;
	default:
		without_mii = 0;
		break;
	}
	return without_mii;
}

#define NCONFIG_AUTO_SWITCH	0x0080
#define MII_NSC_CONG		MII_RESV1
#define NSC_CONG_ENABLE		0x0100
#define NSC_CONG_TXREADY	0x0400
#define ADVERTISE_FC_SUPPORTED	0x0400
static int e100_phy_init(struct nic *nic)
{
	struct net_device *netdev = nic->netdev;
	u32 addr;
	u16 bmcr, stat, id_lo, id_hi, cong;

	/* Discover phy addr by searching addrs in order {1,0,2,..., 31} */
	for (addr = 0; addr < 32; addr++) {
		nic->mii.phy_id = (addr == 0) ? 1 : (addr == 1) ? 0 : addr;
		bmcr = mdio_read(netdev, nic->mii.phy_id, MII_BMCR);
		stat = mdio_read(netdev, nic->mii.phy_id, MII_BMSR);
		stat = mdio_read(netdev, nic->mii.phy_id, MII_BMSR);
		if (!((bmcr == 0xFFFF) || ((stat == 0) && (bmcr == 0))))
			break;
	}
	if (addr == 32) {
		/* uhoh, no PHY detected: check whether we seem to be some
		 * weird, rare variant which is *known* to not have any MII.
		 * But do this AFTER MII checking only, since this does
		 * lookup of EEPROM values which may easily be unreliable. */
		if (e100_phy_check_without_mii(nic))
			return 0; /* simply return and hope for the best */
		else {
			/* for unknown cases log a fatal error */
			netif_err(nic, hw, nic->netdev,
				  "Failed to locate any known PHY, aborting\n");
			return -EAGAIN;
		}
	} else
		netif_printk(nic, hw, KERN_DEBUG, nic->netdev,
			     "phy_addr = %d\n", nic->mii.phy_id);

	/* Get phy ID */
	id_lo = mdio_read(netdev, nic->mii.phy_id, MII_PHYSID1);
	id_hi = mdio_read(netdev, nic->mii.phy_id, MII_PHYSID2);
	nic->phy = (u32)id_hi << 16 | (u32)id_lo;
	netif_printk(nic, hw, KERN_DEBUG, nic->netdev,
		     "phy ID = 0x%08X\n", nic->phy);

	/* Select the phy and isolate the rest */
	for (addr = 0; addr < 32; addr++) {
		if (addr != nic->mii.phy_id) {
			mdio_write(netdev, addr, MII_BMCR, BMCR_ISOLATE);
		} else if (nic->phy != phy_82552_v) {
			bmcr = mdio_read(netdev, addr, MII_BMCR);
			mdio_write(netdev, addr, MII_BMCR,
				bmcr & ~BMCR_ISOLATE);
		}
	}
	/*
	 * Workaround for 82552:
	 * Clear the ISOLATE bit on selected phy_id last (mirrored on all
	 * other phy_id's) using bmcr value from addr discovery loop above.
	 */
	if (nic->phy == phy_82552_v)
		mdio_write(netdev, nic->mii.phy_id, MII_BMCR,
			bmcr & ~BMCR_ISOLATE);

	/* Handle National tx phys */
#define NCS_PHY_MODEL_MASK	0xFFF0FFFF
	if ((nic->phy & NCS_PHY_MODEL_MASK) == phy_nsc_tx) {
		/* Disable congestion control */
		cong = mdio_read(netdev, nic->mii.phy_id, MII_NSC_CONG);
		cong |= NSC_CONG_TXREADY;
		cong &= ~NSC_CONG_ENABLE;
		mdio_write(netdev, nic->mii.phy_id, MII_NSC_CONG, cong);
	}

	if (nic->phy == phy_82552_v) {
		u16 advert = mdio_read(netdev, nic->mii.phy_id, MII_ADVERTISE);

		/* assign special tweaked mdio_ctrl() function */
		nic->mdio_ctrl = mdio_ctrl_phy_82552_v;

		/* Workaround Si not advertising flow-control during autoneg */
		advert |= ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM;
		mdio_write(netdev, nic->mii.phy_id, MII_ADVERTISE, advert);

		/* Reset for the above changes to take effect */
		bmcr = mdio_read(netdev, nic->mii.phy_id, MII_BMCR);
		bmcr |= BMCR_RESET;
		mdio_write(netdev, nic->mii.phy_id, MII_BMCR, bmcr);
	} else if ((nic->mac >= mac_82550_D102) || ((nic->flags & ich) &&
	   (mdio_read(netdev, nic->mii.phy_id, MII_TPISTATUS) & 0x8000) &&
		!(nic->eeprom[eeprom_cnfg_mdix] & eeprom_mdix_enabled))) {
		/* enable/disable MDI/MDI-X auto-switching. */
		mdio_write(netdev, nic->mii.phy_id, MII_NCONFIG,
				nic->mii.force_media ? 0 : NCONFIG_AUTO_SWITCH);
	}

	return 0;
}

static int e100_hw_init(struct nic *nic)
{
	int err = 0;

	e100_hw_reset(nic);

	netif_err(nic, hw, nic->netdev, "e100_hw_init\n");
	if (!in_interrupt() && (err = e100_self_test(nic)))
		return err;

	if ((err = e100_phy_init(nic)))
		return err;
	if ((err = e100_exec_cmd(nic, cuc_load_base, 0)))
		return err;
	if ((err = e100_exec_cmd(nic, ruc_load_base, 0)))
		return err;
	if ((err = e100_load_ucode_wait(nic)))
		return err;
	if ((err = e100_exec_cb(nic, NULL, e100_configure)))
		return err;
	if ((err = e100_exec_cb(nic, NULL, e100_setup_iaaddr)))
		return err;
	if ((err = e100_exec_cmd(nic, cuc_dump_addr,
		nic->dma_addr + offsetof(struct mem, stats))))
		return err;
	if ((err = e100_exec_cmd(nic, cuc_dump_reset, 0)))
		return err;

	e100_disable_irq(nic);

	return 0;
}

static int e100_multi(struct nic *nic, struct cb *cb, struct sk_buff *skb)
{
	struct net_device *netdev = nic->netdev;
	struct netdev_hw_addr *ha;
	u16 i, count = min(netdev_mc_count(netdev), E100_MAX_MULTICAST_ADDRS);

	cb->command = cpu_to_le16(cb_multi);
	cb->u.multi.count = cpu_to_le16(count * ETH_ALEN);
	i = 0;
	netdev_for_each_mc_addr(ha, netdev) {
		if (i == count)
			break;
		memcpy(&cb->u.multi.addr[i++ * ETH_ALEN], &ha->addr,
			ETH_ALEN);
	}
	return 0;
}

static void e100_set_multicast_list(struct net_device *netdev)
{
	struct nic *nic = netdev_priv(netdev);

	netif_printk(nic, hw, KERN_DEBUG, nic->netdev,
		     "mc_count=%d, flags=0x%04X\n",
		     netdev_mc_count(netdev), netdev->flags);

	if (netdev->flags & IFF_PROMISC)
		nic->flags |= promiscuous;
	else
		nic->flags &= ~promiscuous;

	if (netdev->flags & IFF_ALLMULTI ||
		netdev_mc_count(netdev) > E100_MAX_MULTICAST_ADDRS)
		nic->flags |= multicast_all;
	else
		nic->flags &= ~multicast_all;

	e100_exec_cb(nic, NULL, e100_configure);
	e100_exec_cb(nic, NULL, e100_multi);
}

static void e100_update_stats(struct nic *nic)
{
	struct net_device *dev = nic->netdev;
	struct net_device_stats *ns = &dev->stats;
	struct stats *s = &nic->mem->stats;
	__le32 *complete = (nic->mac < mac_82558_D101_A4) ? &s->fc_xmt_pause :
		(nic->mac < mac_82559_D101M) ? (__le32 *)&s->xmt_tco_frames :
		&s->complete;

	/* Device's stats reporting may take several microseconds to
	 * complete, so we're always waiting for results of the
	 * previous command. */

	if (*complete == cpu_to_le32(cuc_dump_reset_complete)) {
		*complete = 0;
		nic->tx_frames = le32_to_cpu(s->tx_good_frames);
		nic->tx_collisions = le32_to_cpu(s->tx_total_collisions);
		ns->tx_aborted_errors += le32_to_cpu(s->tx_max_collisions);
		ns->tx_window_errors += le32_to_cpu(s->tx_late_collisions);
		ns->tx_carrier_errors += le32_to_cpu(s->tx_lost_crs);
		ns->tx_fifo_errors += le32_to_cpu(s->tx_underruns);
		ns->collisions += nic->tx_collisions;
		ns->tx_errors += le32_to_cpu(s->tx_max_collisions) +
			le32_to_cpu(s->tx_lost_crs);
		nic->rx_short_frame_errors +=
			le32_to_cpu(s->rx_short_frame_errors);
		ns->rx_length_errors = nic->rx_short_frame_errors +
			nic->rx_over_length_errors;
		ns->rx_crc_errors += le32_to_cpu(s->rx_crc_errors);
		ns->rx_frame_errors += le32_to_cpu(s->rx_alignment_errors);
		ns->rx_over_errors += le32_to_cpu(s->rx_overrun_errors);
		ns->rx_fifo_errors += le32_to_cpu(s->rx_overrun_errors);
		ns->rx_missed_errors += le32_to_cpu(s->rx_resource_errors);
		ns->rx_errors += le32_to_cpu(s->rx_crc_errors) +
			le32_to_cpu(s->rx_alignment_errors) +
			le32_to_cpu(s->rx_short_frame_errors) +
			le32_to_cpu(s->rx_cdt_errors);
		nic->tx_deferred += le32_to_cpu(s->tx_deferred);
		nic->tx_single_collisions +=
			le32_to_cpu(s->tx_single_collisions);
		nic->tx_multiple_collisions +=
			le32_to_cpu(s->tx_multiple_collisions);
		if (nic->mac >= mac_82558_D101_A4) {
			nic->tx_fc_pause += le32_to_cpu(s->fc_xmt_pause);
			nic->rx_fc_pause += le32_to_cpu(s->fc_rcv_pause);
			nic->rx_fc_unsupported +=
				le32_to_cpu(s->fc_rcv_unsupported);
			if (nic->mac >= mac_82559_D101M) {
				nic->tx_tco_frames +=
					le16_to_cpu(s->xmt_tco_frames);
				nic->rx_tco_frames +=
					le16_to_cpu(s->rcv_tco_frames);
			}
		}
	}


	if (e100_exec_cmd(nic, cuc_dump_reset, 0))
		netif_printk(nic, tx_err, KERN_DEBUG, nic->netdev,
			     "exec cuc_dump_reset failed\n");
}

static void e100_adjust_adaptive_ifs(struct nic *nic, int speed, int duplex)
{
	/* Adjust inter-frame-spacing (IFS) between two transmits if
	 * we're getting collisions on a half-duplex connection. */

	if (duplex == DUPLEX_HALF) {
		u32 prev = nic->adaptive_ifs;
		u32 min_frames = (speed == SPEED_100) ? 1000 : 100;

		if ((nic->tx_frames / 32 < nic->tx_collisions) &&
		   (nic->tx_frames > min_frames)) {
			if (nic->adaptive_ifs < 60)
				nic->adaptive_ifs += 5;
		} else if (nic->tx_frames < min_frames) {
			if (nic->adaptive_ifs >= 5)
				nic->adaptive_ifs -= 5;
		}
		if (nic->adaptive_ifs != prev)
			e100_exec_cb(nic, NULL, e100_configure);
	}
}

static void e100_watchdog(unsigned long data)
{
	struct nic *nic = (struct nic *)data;
	struct ethtool_cmd cmd = { .cmd = ETHTOOL_GSET };
	u32 speed;

	netif_printk(nic, timer, KERN_DEBUG, nic->netdev,
		     "right now = %ld\n", jiffies);

	/* mii library handles link maintenance tasks */

	mii_ethtool_gset(&nic->mii, &cmd);
	speed = ethtool_cmd_speed(&cmd);

	if (mii_link_ok(&nic->mii) && !netif_carrier_ok(nic->netdev)) {
		netdev_info(nic->netdev, "NIC Link is Up %u Mbps %s Duplex\n",
			    speed == SPEED_100 ? 100 : 10,
			    cmd.duplex == DUPLEX_FULL ? "Full" : "Half");
	} else if (!mii_link_ok(&nic->mii) && netif_carrier_ok(nic->netdev)) {
		netdev_info(nic->netdev, "NIC Link is Down\n");
	}

	mii_check_link(&nic->mii);

	/* Software generated interrupt to recover from (rare) Rx
	 * allocation failure.
	 * Unfortunately have to use a spinlock to not re-enable interrupts
	 * accidentally, due to hardware that shares a register between the
	 * interrupt mask bit and the SW Interrupt generation bit */
	spin_lock_irq(&nic->cmd_lock);
	iowrite8(ioread8(&nic->csr->scb.cmd_hi) | irq_sw_gen,&nic->csr->scb.cmd_hi);
	e100_write_flush(nic);
	spin_unlock_irq(&nic->cmd_lock);

	e100_update_stats(nic);
	e100_adjust_adaptive_ifs(nic, speed, cmd.duplex);

	if (nic->mac <= mac_82557_D100_C)
		/* Issue a multicast command to workaround a 557 lock up */
		e100_set_multicast_list(nic->netdev);

	if (nic->flags & ich && speed == SPEED_10 && cmd.duplex == DUPLEX_HALF)
		/* Need SW workaround for ICH[x] 10Mbps/half duplex Tx hang. */
		nic->flags |= ich_10h_workaround;
	else
		nic->flags &= ~ich_10h_workaround;

	mod_timer(&nic->watchdog,
		  round_jiffies(jiffies + E100_WATCHDOG_PERIOD));
}

static int e100_xmit_prepare(struct nic *nic, struct cb *cb,
	struct sk_buff *skb)
{
	dma_addr_t dma_addr;
	cb->command = nic->tx_command;

	dma_addr = pci_map_single(nic->pdev,
				  skb->data, skb->len, PCI_DMA_TODEVICE);
	/* If we can't map the skb, have the upper layer try later */
	if (pci_dma_mapping_error(nic->pdev, dma_addr))
		return -ENOMEM;

	/*
	 * Use the last 4 bytes of the SKB payload packet as the CRC, used for
	 * testing, ie sending frames with bad CRC.
	 */
	if (unlikely(skb->no_fcs))
		cb->command |= cpu_to_le16(cb_tx_nc);
	else
		cb->command &= ~cpu_to_le16(cb_tx_nc);

	/* interrupt every 16 packets regardless of delay */
	if ((nic->cbs_avail & ~15) == nic->cbs_avail)
		cb->command |= cpu_to_le16(cb_i);
	cb->u.tcb.tbd_array = cb->dma_addr + offsetof(struct cb, u.tcb.tbd);
	cb->u.tcb.tcb_byte_count = 0;
	cb->u.tcb.threshold = nic->tx_threshold;
	cb->u.tcb.tbd_count = 1;
	cb->u.tcb.tbd.buf_addr = cpu_to_le32(dma_addr);
	cb->u.tcb.tbd.size = cpu_to_le16(skb->len);
	skb_tx_timestamp(skb);
	return 0;
}

static netdev_tx_t e100_xmit_frame(struct sk_buff *skb,
				   struct net_device *netdev)
{
	struct nic *nic = netdev_priv(netdev);
	int err;

	if (nic->flags & ich_10h_workaround) {
		/* SW workaround for ICH[x] 10Mbps/half duplex Tx hang.
		   Issue a NOP command followed by a 1us delay before
		   issuing the Tx command. */
		if (e100_exec_cmd(nic, cuc_nop, 0))
			netif_printk(nic, tx_err, KERN_DEBUG, nic->netdev,
				     "exec cuc_nop failed\n");
		udelay(1);
	}

	err = e100_exec_cb(nic, skb, e100_xmit_prepare);

	switch (err) {
	case -ENOSPC:
		/* We queued the skb, but now we're out of space. */
		netif_printk(nic, tx_err, KERN_DEBUG, nic->netdev,
			     "No space for CB\n");
		netif_stop_queue(netdev);
		break;
	case -ENOMEM:
		/* This is a hard error - log it. */
		netif_printk(nic, tx_err, KERN_DEBUG, nic->netdev,
			     "Out of Tx resources, returning skb\n");
		netif_stop_queue(netdev);
		return NETDEV_TX_BUSY;
	}

	return NETDEV_TX_OK;
}

static int e100_tx_clean(struct nic *nic)
{
	struct net_device *dev = nic->netdev;
	struct cb *cb;
	int tx_cleaned = 0;

	spin_lock(&nic->cb_lock);

	/* Clean CBs marked complete */
	for (cb = nic->cb_to_clean;
	    cb->status & cpu_to_le16(cb_complete);
	    cb = nic->cb_to_clean = cb->next) {
		rmb(); /* read skb after status */
		netif_printk(nic, tx_done, KERN_DEBUG, nic->netdev,
			     "cb[%d]->status = 0x%04X\n",
			     (int)(((void*)cb - (void*)nic->cbs)/sizeof(struct cb)),
			     cb->status);

		if (likely(cb->skb != NULL)) {
			dev->stats.tx_packets++;
			dev->stats.tx_bytes += cb->skb->len;

			pci_unmap_single(nic->pdev,
				le32_to_cpu(cb->u.tcb.tbd.buf_addr),
				le16_to_cpu(cb->u.tcb.tbd.size),
				PCI_DMA_TODEVICE);
			dev_kfree_skb_any(cb->skb);
			cb->skb = NULL;
			tx_cleaned = 1;
		}
		cb->status = 0;
		nic->cbs_avail++;
	}

	spin_unlock(&nic->cb_lock);

	/* Recover from running out of Tx resources in xmit_frame */
	if (unlikely(tx_cleaned && netif_queue_stopped(nic->netdev)))
		netif_wake_queue(nic->netdev);

	return tx_cleaned;
}

static void e100_clean_cbs(struct nic *nic)
{
	if (nic->cbs) {
		while (nic->cbs_avail != nic->params.cbs.count) {
			struct cb *cb = nic->cb_to_clean;
			if (cb->skb) {
				pci_unmap_single(nic->pdev,
					le32_to_cpu(cb->u.tcb.tbd.buf_addr),
					le16_to_cpu(cb->u.tcb.tbd.size),
					PCI_DMA_TODEVICE);
				dev_kfree_skb(cb->skb);
			}
			nic->cb_to_clean = nic->cb_to_clean->next;
			nic->cbs_avail++;
		}
		pci_pool_free(nic->cbs_pool, nic->cbs, nic->cbs_dma_addr);
		nic->cbs = NULL;
		nic->cbs_avail = 0;
	}
	nic->cuc_cmd = cuc_start;
	nic->cb_to_use = nic->cb_to_send = nic->cb_to_clean =
		nic->cbs;
}

static int e100_alloc_cbs(struct nic *nic)
{
	struct cb *cb;
	unsigned int i, count = nic->params.cbs.count;

	nic->cuc_cmd = cuc_start;
	nic->cb_to_use = nic->cb_to_send = nic->cb_to_clean = NULL;
	nic->cbs_avail = 0;

	nic->cbs = pci_pool_alloc(nic->cbs_pool, GFP_KERNEL,
				  &nic->cbs_dma_addr);
	if (!nic->cbs)
		return -ENOMEM;
	memset(nic->cbs, 0, count * sizeof(struct cb));

	for (cb = nic->cbs, i = 0; i < count; cb++, i++) {
		cb->next = (i + 1 < count) ? cb + 1 : nic->cbs;
		cb->prev = (i == 0) ? nic->cbs + count - 1 : cb - 1;

		cb->dma_addr = nic->cbs_dma_addr + i * sizeof(struct cb);
		cb->link = cpu_to_le32(nic->cbs_dma_addr +
			((i+1) % count) * sizeof(struct cb));
	}

	nic->cb_to_use = nic->cb_to_send = nic->cb_to_clean = nic->cbs;
	nic->cbs_avail = count;

	return 0;
}

static inline void e100_start_receiver(struct nic *nic, struct rx *rx)
{
	if (!nic->rxs) return;
	if (RU_SUSPENDED != nic->ru_running) return;

	/* handle init time starts */
	if (!rx) rx = nic->rxs;

	/* (Re)start RU if suspended or idle and RFA is non-NULL */
	if (rx->skb) {
		e100_exec_cmd(nic, ruc_start, rx->dma_addr);
		nic->ru_running = RU_RUNNING;
	}
}

#define RFD_BUF_LEN (sizeof(struct rfd) + VLAN_ETH_FRAME_LEN + ETH_FCS_LEN)
static int e100_rx_alloc_skb(struct nic *nic, struct rx *rx)
{
	if (!(rx->skb = netdev_alloc_skb_ip_align(nic->netdev, RFD_BUF_LEN)))
		return -ENOMEM;

	/* Init, and map the RFD. */
	skb_copy_to_linear_data(rx->skb, &nic->blank_rfd, sizeof(struct rfd));
	rx->dma_addr = pci_map_single(nic->pdev, rx->skb->data,
		RFD_BUF_LEN, PCI_DMA_BIDIRECTIONAL);

	if (pci_dma_mapping_error(nic->pdev, rx->dma_addr)) {
		dev_kfree_skb_any(rx->skb);
		rx->skb = NULL;
		rx->dma_addr = 0;
		return -ENOMEM;
	}

	/* Link the RFD to end of RFA by linking previous RFD to
	 * this one.  We are safe to touch the previous RFD because
	 * it is protected by the before last buffer's el bit being set */
	if (rx->prev->skb) {
		struct rfd *prev_rfd = (struct rfd *)rx->prev->skb->data;
		put_unaligned_le32(rx->dma_addr, &prev_rfd->link);
		pci_dma_sync_single_for_device(nic->pdev, rx->prev->dma_addr,
			sizeof(struct rfd), PCI_DMA_BIDIRECTIONAL);
	}

	return 0;
}

static int e100_rx_indicate(struct nic *nic, struct rx *rx,
	unsigned int *work_done, unsigned int work_to_do)
{
	struct net_device *dev = nic->netdev;
	struct sk_buff *skb = rx->skb;
	struct rfd *rfd = (struct rfd *)skb->data;
	u16 rfd_status, actual_size;
	u16 fcs_pad = 0;

	if (unlikely(work_done && *work_done >= work_to_do))
		return -EAGAIN;

	/* Need to sync before taking a peek at cb_complete bit */
	pci_dma_sync_single_for_cpu(nic->pdev, rx->dma_addr,
		sizeof(struct rfd), PCI_DMA_BIDIRECTIONAL);
	rfd_status = le16_to_cpu(rfd->status);

	netif_printk(nic, rx_status, KERN_DEBUG, nic->netdev,
		     "status=0x%04X\n", rfd_status);
	rmb(); /* read size after status bit */

	/* If data isn't ready, nothing to indicate */
	if (unlikely(!(rfd_status & cb_complete))) {
		/* If the next buffer has the el bit, but we think the receiver
		 * is still running, check to see if it really stopped while
		 * we had interrupts off.
		 * This allows for a fast restart without re-enabling
		 * interrupts */
		if ((le16_to_cpu(rfd->command) & cb_el) &&
		    (RU_RUNNING == nic->ru_running))

			if (ioread8(&nic->csr->scb.status) & rus_no_res)
				nic->ru_running = RU_SUSPENDED;
		pci_dma_sync_single_for_device(nic->pdev, rx->dma_addr,
					       sizeof(struct rfd),
					       PCI_DMA_FROMDEVICE);
		return -ENODATA;
	}

	/* Get actual data size */
	if (unlikely(dev->features & NETIF_F_RXFCS))
		fcs_pad = 4;
	actual_size = le16_to_cpu(rfd->actual_size) & 0x3FFF;
	if (unlikely(actual_size > RFD_BUF_LEN - sizeof(struct rfd)))
		actual_size = RFD_BUF_LEN - sizeof(struct rfd);

	/* Get data */
	pci_unmap_single(nic->pdev, rx->dma_addr,
		RFD_BUF_LEN, PCI_DMA_BIDIRECTIONAL);

	/* If this buffer has the el bit, but we think the receiver
	 * is still running, check to see if it really stopped while
	 * we had interrupts off.
	 * This allows for a fast restart without re-enabling interrupts.
	 * This can happen when the RU sees the size change but also sees
	 * the el bit set. */
	if ((le16_to_cpu(rfd->command) & cb_el) &&
	    (RU_RUNNING == nic->ru_running)) {

	    if (ioread8(&nic->csr->scb.status) & rus_no_res)
		nic->ru_running = RU_SUSPENDED;
	}

	/* Pull off the RFD and put the actual data (minus eth hdr) */
	skb_reserve(skb, sizeof(struct rfd));
	skb_put(skb, actual_size);
	skb->protocol = eth_type_trans(skb, nic->netdev);

	/* If we are receiving all frames, then don't bother
	 * checking for errors.
	 */
	if (unlikely(dev->features & NETIF_F_RXALL)) {
		if (actual_size > ETH_DATA_LEN + VLAN_ETH_HLEN + fcs_pad)
			/* Received oversized frame, but keep it. */
			nic->rx_over_length_errors++;
		goto process_skb;
	}

	if (unlikely(!(rfd_status & cb_ok))) {
		/* Don't indicate if hardware indicates errors */
		dev_kfree_skb_any(skb);
	} else if (actual_size > ETH_DATA_LEN + VLAN_ETH_HLEN + fcs_pad) {
		/* Don't indicate oversized frames */
		nic->rx_over_length_errors++;
		dev_kfree_skb_any(skb);
	} else {
process_skb:
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += (actual_size - fcs_pad);
		netif_receive_skb(skb);
		if (work_done)
			(*work_done)++;
	}

	rx->skb = NULL;

	return 0;
}

static void e100_rx_clean(struct nic *nic, unsigned int *work_done,
	unsigned int work_to_do)
{
	struct rx *rx;
	int restart_required = 0, err = 0;
	struct rx *old_before_last_rx, *new_before_last_rx;
	struct rfd *old_before_last_rfd, *new_before_last_rfd;

	/* Indicate newly arrived packets */
	for (rx = nic->rx_to_clean; rx->skb; rx = nic->rx_to_clean = rx->next) {
		err = e100_rx_indicate(nic, rx, work_done, work_to_do);
		/* Hit quota or no more to clean */
		if (-EAGAIN == err || -ENODATA == err)
			break;
	}


	/* On EAGAIN, hit quota so have more work to do, restart once
	 * cleanup is complete.
	 * Else, are we already rnr? then pay attention!!! this ensures that
	 * the state machine progression never allows a start with a
	 * partially cleaned list, avoiding a race between hardware
	 * and rx_to_clean when in NAPI mode */
	if (-EAGAIN != err && RU_SUSPENDED == nic->ru_running)
		restart_required = 1;

	old_before_last_rx = nic->rx_to_use->prev->prev;
	old_before_last_rfd = (struct rfd *)old_before_last_rx->skb->data;

	/* Alloc new skbs to refill list */
	for (rx = nic->rx_to_use; !rx->skb; rx = nic->rx_to_use = rx->next) {
		if (unlikely(e100_rx_alloc_skb(nic, rx)))
			break; /* Better luck next time (see watchdog) */
	}

	new_before_last_rx = nic->rx_to_use->prev->prev;
	if (new_before_last_rx != old_before_last_rx) {
		/* Set the el-bit on the buffer that is before the last buffer.
		 * This lets us update the next pointer on the last buffer
		 * without worrying about hardware touching it.
		 * We set the size to 0 to prevent hardware from touching this
		 * buffer.
		 * When the hardware hits the before last buffer with el-bit
		 * and size of 0, it will RNR interrupt, the RUS will go into
		 * the No Resources state.  It will not complete nor write to
		 * this buffer. */
		new_before_last_rfd =
			(struct rfd *)new_before_last_rx->skb->data;
		new_before_last_rfd->size = 0;
		new_before_last_rfd->command |= cpu_to_le16(cb_el);
		pci_dma_sync_single_for_device(nic->pdev,
			new_before_last_rx->dma_addr, sizeof(struct rfd),
			PCI_DMA_BIDIRECTIONAL);

		/* Now that we have a new stopping point, we can clear the old
		 * stopping point.  We must sync twice to get the proper
		 * ordering on the hardware side of things. */
		old_before_last_rfd->command &= ~cpu_to_le16(cb_el);
		pci_dma_sync_single_for_device(nic->pdev,
			old_before_last_rx->dma_addr, sizeof(struct rfd),
			PCI_DMA_BIDIRECTIONAL);
		old_before_last_rfd->size = cpu_to_le16(VLAN_ETH_FRAME_LEN
							+ ETH_FCS_LEN);
		pci_dma_sync_single_for_device(nic->pdev,
			old_before_last_rx->dma_addr, sizeof(struct rfd),
			PCI_DMA_BIDIRECTIONAL);
	}

	if (restart_required) {
		// ack the rnr?
		iowrite8(stat_ack_rnr, &nic->csr->scb.stat_ack);
		e100_start_receiver(nic, nic->rx_to_clean);
		if (work_done)
			(*work_done)++;
	}
}

static void e100_rx_clean_list(struct nic *nic)
{
	struct rx *rx;
	unsigned int i, count = nic->params.rfds.count;

	nic->ru_running = RU_UNINITIALIZED;

	if (nic->rxs) {
		for (rx = nic->rxs, i = 0; i < count; rx++, i++) {
			if (rx->skb) {
				pci_unmap_single(nic->pdev, rx->dma_addr,
					RFD_BUF_LEN, PCI_DMA_BIDIRECTIONAL);
				dev_kfree_skb(rx->skb);
			}
		}
		kfree(nic->rxs);
		nic->rxs = NULL;
	}

	nic->rx_to_use = nic->rx_to_clean = NULL;
}

static int e100_rx_alloc_list(struct nic *nic)
{
	struct rx *rx;
	unsigned int i, count = nic->params.rfds.count;
	struct rfd *before_last;

	nic->rx_to_use = nic->rx_to_clean = NULL;
	nic->ru_running = RU_UNINITIALIZED;

	if (!(nic->rxs = kcalloc(count, sizeof(struct rx), GFP_ATOMIC)))
		return -ENOMEM;

	for (rx = nic->rxs, i = 0; i < count; rx++, i++) {
		rx->next = (i + 1 < count) ? rx + 1 : nic->rxs;
		rx->prev = (i == 0) ? nic->rxs + count - 1 : rx - 1;
		if (e100_rx_alloc_skb(nic, rx)) {
			e100_rx_clean_list(nic);
			return -ENOMEM;
		}
	}
	/* Set the el-bit on the buffer that is before the last buffer.
	 * This lets us update the next pointer on the last buffer without
	 * worrying about hardware touching it.
	 * We set the size to 0 to prevent hardware from touching this buffer.
	 * When the hardware hits the before last buffer with el-bit and size
	 * of 0, it will RNR interrupt, the RU will go into the No Resources
	 * state.  It will not complete nor write to this buffer. */
	rx = nic->rxs->prev->prev;
	before_last = (struct rfd *)rx->skb->data;
	before_last->command |= cpu_to_le16(cb_el);
	before_last->size = 0;
	pci_dma_sync_single_for_device(nic->pdev, rx->dma_addr,
		sizeof(struct rfd), PCI_DMA_BIDIRECTIONAL);

	nic->rx_to_use = nic->rx_to_clean = nic->rxs;
	nic->ru_running = RU_SUSPENDED;

	return 0;
}

static irqreturn_t e100_intr(int irq, void *dev_id)
{
	struct net_device *netdev = dev_id;
	struct nic *nic = netdev_priv(netdev);
	u8 stat_ack = ioread8(&nic->csr->scb.stat_ack);

	netif_printk(nic, intr, KERN_DEBUG, nic->netdev,
		     "stat_ack = 0x%02X\n", stat_ack);

	if (stat_ack == stat_ack_not_ours ||	/* Not our interrupt */
	   stat_ack == stat_ack_not_present)	/* Hardware is ejected */
		return IRQ_NONE;

	/* Ack interrupt(s) */
	iowrite8(stat_ack, &nic->csr->scb.stat_ack);

	/* We hit Receive No Resource (RNR); restart RU after cleaning */
	if (stat_ack & stat_ack_rnr)
		nic->ru_running = RU_SUSPENDED;

	if (likely(napi_schedule_prep(&nic->napi))) {
		e100_disable_irq(nic);
		__napi_schedule(&nic->napi);
	}

	return IRQ_HANDLED;
}

static int e100_poll(struct napi_struct *napi, int budget)
{
	struct nic *nic = container_of(napi, struct nic, napi);
	unsigned int work_done = 0;

	e100_rx_clean(nic, &work_done, budget);
	e100_tx_clean(nic);

	/* If budget not fully consumed, exit the polling mode */
	if (work_done < budget) {
		napi_complete(napi);
		e100_enable_irq(nic);
	}

	return work_done;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void e100_netpoll(struct net_device *netdev)
{
	struct nic *nic = netdev_priv(netdev);

	e100_disable_irq(nic);
	e100_intr(nic->pdev->irq, netdev);
	e100_tx_clean(nic);
	e100_enable_irq(nic);
}
#endif

static int e100_set_mac_address(struct net_device *netdev, void *p)
{
	struct nic *nic = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	e100_exec_cb(nic, NULL, e100_setup_iaaddr);

	return 0;
}

static int e100_change_mtu(struct net_device *netdev, int new_mtu)
{
	if (new_mtu < ETH_ZLEN || new_mtu > ETH_DATA_LEN)
		return -EINVAL;
	netdev->mtu = new_mtu;
	return 0;
}

static int e100_asf(struct nic *nic)
{
	/* ASF can be enabled from eeprom */
	return (nic->pdev->device >= 0x1050) && (nic->pdev->device <= 0x1057) &&
	   (nic->eeprom[eeprom_config_asf] & eeprom_asf) &&
	   !(nic->eeprom[eeprom_config_asf] & eeprom_gcl) &&
	   ((nic->eeprom[eeprom_smbus_addr] & 0xFF) != 0xFE);
}

static int e100_up(struct nic *nic)
{
	int err;

	if ((err = e100_rx_alloc_list(nic)))
		return err;
	if ((err = e100_alloc_cbs(nic)))
		goto err_rx_clean_list;
	if ((err = e100_hw_init(nic)))
		goto err_clean_cbs;
	e100_set_multicast_list(nic->netdev);
	e100_start_receiver(nic, NULL);
	mod_timer(&nic->watchdog, jiffies);
	if ((err = request_irq(nic->pdev->irq, e100_intr, IRQF_SHARED,
		nic->netdev->name, nic->netdev)))
		goto err_no_irq;
	netif_wake_queue(nic->netdev);
	napi_enable(&nic->napi);
	/* enable ints _after_ enabling poll, preventing a race between
	 * disable ints+schedule */
	e100_enable_irq(nic);
	return 0;

err_no_irq:
	del_timer_sync(&nic->watchdog);
err_clean_cbs:
	e100_clean_cbs(nic);
err_rx_clean_list:
	e100_rx_clean_list(nic);
	return err;
}

static void e100_down(struct nic *nic)
{
	/* wait here for poll to complete */
	napi_disable(&nic->napi);
	netif_stop_queue(nic->netdev);
	e100_hw_reset(nic);
	free_irq(nic->pdev->irq, nic->netdev);
	del_timer_sync(&nic->watchdog);
	netif_carrier_off(nic->netdev);
	e100_clean_cbs(nic);
	e100_rx_clean_list(nic);
}

static void e100_tx_timeout(struct net_device *netdev)
{
	struct nic *nic = netdev_priv(netdev);

	/* Reset outside of interrupt context, to avoid request_irq
	 * in interrupt context */
	schedule_work(&nic->tx_timeout_task);
}

static void e100_tx_timeout_task(struct work_struct *work)
{
	struct nic *nic = container_of(work, struct nic, tx_timeout_task);
	struct net_device *netdev = nic->netdev;

	netif_printk(nic, tx_err, KERN_DEBUG, nic->netdev,
		     "scb.status=0x%02X\n", ioread8(&nic->csr->scb.status));

	rtnl_lock();
	if (netif_running(netdev)) {
		e100_down(netdev_priv(netdev));
		e100_up(netdev_priv(netdev));
	}
	rtnl_unlock();
}

static int e100_loopback_test(struct nic *nic, enum loopback loopback_mode)
{
	int err;
	struct sk_buff *skb;

	/* Use driver resources to perform internal MAC or PHY
	 * loopback test.  A single packet is prepared and transmitted
	 * in loopback mode, and the test passes if the received
	 * packet compares byte-for-byte to the transmitted packet. */

	if ((err = e100_rx_alloc_list(nic)))
		return err;
	if ((err = e100_alloc_cbs(nic)))
		goto err_clean_rx;

	/* ICH PHY loopback is broken so do MAC loopback instead */
	if (nic->flags & ich && loopback_mode == lb_phy)
		loopback_mode = lb_mac;

	nic->loopback = loopback_mode;
	if ((err = e100_hw_init(nic)))
		goto err_loopback_none;

	if (loopback_mode == lb_phy)
		mdio_write(nic->netdev, nic->mii.phy_id, MII_BMCR,
			BMCR_LOOPBACK);

	e100_start_receiver(nic, NULL);

	if (!(skb = netdev_alloc_skb(nic->netdev, ETH_DATA_LEN))) {
		err = -ENOMEM;
		goto err_loopback_none;
	}
	skb_put(skb, ETH_DATA_LEN);
	memset(skb->data, 0xFF, ETH_DATA_LEN);
	e100_xmit_frame(skb, nic->netdev);

	msleep(10);

	pci_dma_sync_single_for_cpu(nic->pdev, nic->rx_to_clean->dma_addr,
			RFD_BUF_LEN, PCI_DMA_BIDIRECTIONAL);

	if (memcmp(nic->rx_to_clean->skb->data + sizeof(struct rfd),
	   skb->data, ETH_DATA_LEN))
		err = -EAGAIN;

err_loopback_none:
	mdio_write(nic->netdev, nic->mii.phy_id, MII_BMCR, 0);
	nic->loopback = lb_none;
	e100_clean_cbs(nic);
	e100_hw_reset(nic);
err_clean_rx:
	e100_rx_clean_list(nic);
	return err;
}

#define MII_LED_CONTROL	0x1B
#define E100_82552_LED_OVERRIDE 0x19
#define E100_82552_LED_ON       0x000F /* LEDTX and LED_RX both on */
#define E100_82552_LED_OFF      0x000A /* LEDTX and LED_RX both off */

static int e100_get_settings(struct net_device *netdev, struct ethtool_cmd *cmd)
{
	struct nic *nic = netdev_priv(netdev);
	return mii_ethtool_gset(&nic->mii, cmd);
}

static int e100_set_settings(struct net_device *netdev, struct ethtool_cmd *cmd)
{
	struct nic *nic = netdev_priv(netdev);
	int err;

	mdio_write(netdev, nic->mii.phy_id, MII_BMCR, BMCR_RESET);
	err = mii_ethtool_sset(&nic->mii, cmd);
	e100_exec_cb(nic, NULL, e100_configure);

	return err;
}

static void e100_get_drvinfo(struct net_device *netdev,
	struct ethtool_drvinfo *info)
{
	struct nic *nic = netdev_priv(netdev);
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(nic->pdev),
		sizeof(info->bus_info));
}

#define E100_PHY_REGS 0x1C
static int e100_get_regs_len(struct net_device *netdev)
{
	struct nic *nic = netdev_priv(netdev);
	return 1 + E100_PHY_REGS + sizeof(nic->mem->dump_buf);
}

static void e100_get_regs(struct net_device *netdev,
	struct ethtool_regs *regs, void *p)
{
	struct nic *nic = netdev_priv(netdev);
	u32 *buff = p;
	int i;

	regs->version = (1 << 24) | nic->pdev->revision;
	buff[0] = ioread8(&nic->csr->scb.cmd_hi) << 24 |
		ioread8(&nic->csr->scb.cmd_lo) << 16 |
		ioread16(&nic->csr->scb.status);
	for (i = E100_PHY_REGS; i >= 0; i--)
		buff[1 + E100_PHY_REGS - i] =
			mdio_read(netdev, nic->mii.phy_id, i);
	memset(nic->mem->dump_buf, 0, sizeof(nic->mem->dump_buf));
	e100_exec_cb(nic, NULL, e100_dump);
	msleep(10);
	memcpy(&buff[2 + E100_PHY_REGS], nic->mem->dump_buf,
		sizeof(nic->mem->dump_buf));
}

static void e100_get_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct nic *nic = netdev_priv(netdev);
	wol->supported = (nic->mac >= mac_82558_D101_A4) ?  WAKE_MAGIC : 0;
	wol->wolopts = (nic->flags & wol_magic) ? WAKE_MAGIC : 0;
}

static int e100_set_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct nic *nic = netdev_priv(netdev);

	if ((wol->wolopts && wol->wolopts != WAKE_MAGIC) ||
	    !device_can_wakeup(&nic->pdev->dev))
		return -EOPNOTSUPP;

	if (wol->wolopts)
		nic->flags |= wol_magic;
	else
		nic->flags &= ~wol_magic;

	device_set_wakeup_enable(&nic->pdev->dev, wol->wolopts);

	e100_exec_cb(nic, NULL, e100_configure);

	return 0;
}

static u32 e100_get_msglevel(struct net_device *netdev)
{
	struct nic *nic = netdev_priv(netdev);
	return nic->msg_enable;
}

static void e100_set_msglevel(struct net_device *netdev, u32 value)
{
	struct nic *nic = netdev_priv(netdev);
	nic->msg_enable = value;
}

static int e100_nway_reset(struct net_device *netdev)
{
	struct nic *nic = netdev_priv(netdev);
	return mii_nway_restart(&nic->mii);
}

static u32 e100_get_link(struct net_device *netdev)
{
	struct nic *nic = netdev_priv(netdev);
	return mii_link_ok(&nic->mii);
}

static int e100_get_eeprom_len(struct net_device *netdev)
{
	struct nic *nic = netdev_priv(netdev);
	return nic->eeprom_wc << 1;
}

#define E100_EEPROM_MAGIC	0x1234
static int e100_get_eeprom(struct net_device *netdev,
	struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct nic *nic = netdev_priv(netdev);

	eeprom->magic = E100_EEPROM_MAGIC;
	memcpy(bytes, &((u8 *)nic->eeprom)[eeprom->offset], eeprom->len);

	return 0;
}

static int e100_set_eeprom(struct net_device *netdev,
	struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct nic *nic = netdev_priv(netdev);

	if (eeprom->magic != E100_EEPROM_MAGIC)
		return -EINVAL;

	memcpy(&((u8 *)nic->eeprom)[eeprom->offset], bytes, eeprom->len);

	return e100_eeprom_save(nic, eeprom->offset >> 1,
		(eeprom->len >> 1) + 1);
}

static void e100_get_ringparam(struct net_device *netdev,
	struct ethtool_ringparam *ring)
{
	struct nic *nic = netdev_priv(netdev);
	struct param_range *rfds = &nic->params.rfds;
	struct param_range *cbs = &nic->params.cbs;

	ring->rx_max_pending = rfds->max;
	ring->tx_max_pending = cbs->max;
	ring->rx_pending = rfds->count;
	ring->tx_pending = cbs->count;
}

static int e100_set_ringparam(struct net_device *netdev,
	struct ethtool_ringparam *ring)
{
	struct nic *nic = netdev_priv(netdev);
	struct param_range *rfds = &nic->params.rfds;
	struct param_range *cbs = &nic->params.cbs;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	if (netif_running(netdev))
		e100_down(nic);
	rfds->count = max(ring->rx_pending, rfds->min);
	rfds->count = min(rfds->count, rfds->max);
	cbs->count = max(ring->tx_pending, cbs->min);
	cbs->count = min(cbs->count, cbs->max);
	netif_info(nic, drv, nic->netdev, "Ring Param settings: rx: %d, tx %d\n",
		   rfds->count, cbs->count);
	if (netif_running(netdev))
		e100_up(nic);

	return 0;
}

static const char e100_gstrings_test[][ETH_GSTRING_LEN] = {
	"Link test     (on/offline)",
	"Eeprom test   (on/offline)",
	"Self test        (offline)",
	"Mac loopback     (offline)",
	"Phy loopback     (offline)",
};
#define E100_TEST_LEN	ARRAY_SIZE(e100_gstrings_test)

static void e100_diag_test(struct net_device *netdev,
	struct ethtool_test *test, u64 *data)
{
	struct ethtool_cmd cmd;
	struct nic *nic = netdev_priv(netdev);
	int i, err;

	memset(data, 0, E100_TEST_LEN * sizeof(u64));
	data[0] = !mii_link_ok(&nic->mii);
	data[1] = e100_eeprom_load(nic);
	if (test->flags & ETH_TEST_FL_OFFLINE) {

		/* save speed, duplex & autoneg settings */
		err = mii_ethtool_gset(&nic->mii, &cmd);

		if (netif_running(netdev))
			e100_down(nic);
		data[2] = e100_self_test(nic);
		data[3] = e100_loopback_test(nic, lb_mac);
		data[4] = e100_loopback_test(nic, lb_phy);

		/* restore speed, duplex & autoneg settings */
		err = mii_ethtool_sset(&nic->mii, &cmd);

		if (netif_running(netdev))
			e100_up(nic);
	}
	for (i = 0; i < E100_TEST_LEN; i++)
		test->flags |= data[i] ? ETH_TEST_FL_FAILED : 0;

	msleep_interruptible(4 * 1000);
}

static int e100_set_phys_id(struct net_device *netdev,
			    enum ethtool_phys_id_state state)
{
	struct nic *nic = netdev_priv(netdev);
	enum led_state {
		led_on     = 0x01,
		led_off    = 0x04,
		led_on_559 = 0x05,
		led_on_557 = 0x07,
	};
	u16 led_reg = (nic->phy == phy_82552_v) ? E100_82552_LED_OVERRIDE :
		MII_LED_CONTROL;
	u16 leds = 0;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		return 2;

	case ETHTOOL_ID_ON:
		leds = (nic->phy == phy_82552_v) ? E100_82552_LED_ON :
		       (nic->mac < mac_82559_D101M) ? led_on_557 : led_on_559;
		break;

	case ETHTOOL_ID_OFF:
		leds = (nic->phy == phy_82552_v) ? E100_82552_LED_OFF : led_off;
		break;

	case ETHTOOL_ID_INACTIVE:
		break;
	}

	mdio_write(netdev, nic->mii.phy_id, led_reg, leds);
	return 0;
}

static const char e100_gstrings_stats[][ETH_GSTRING_LEN] = {
	"rx_packets", "tx_packets", "rx_bytes", "tx_bytes", "rx_errors",
	"tx_errors", "rx_dropped", "tx_dropped", "multicast", "collisions",
	"rx_length_errors", "rx_over_errors", "rx_crc_errors",
	"rx_frame_errors", "rx_fifo_errors", "rx_missed_errors",
	"tx_aborted_errors", "tx_carrier_errors", "tx_fifo_errors",
	"tx_heartbeat_errors", "tx_window_errors",
	/* device-specific stats */
	"tx_deferred", "tx_single_collisions", "tx_multi_collisions",
	"tx_flow_control_pause", "rx_flow_control_pause",
	"rx_flow_control_unsupported", "tx_tco_packets", "rx_tco_packets",
	"rx_short_frame_errors", "rx_over_length_errors",
};
#define E100_NET_STATS_LEN	21
#define E100_STATS_LEN	ARRAY_SIZE(e100_gstrings_stats)

static int e100_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_TEST:
		return E100_TEST_LEN;
	case ETH_SS_STATS:
		return E100_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void e100_get_ethtool_stats(struct net_device *netdev,
	struct ethtool_stats *stats, u64 *data)
{
	struct nic *nic = netdev_priv(netdev);
	int i;

	for (i = 0; i < E100_NET_STATS_LEN; i++)
		data[i] = ((unsigned long *)&netdev->stats)[i];

	data[i++] = nic->tx_deferred;
	data[i++] = nic->tx_single_collisions;
	data[i++] = nic->tx_multiple_collisions;
	data[i++] = nic->tx_fc_pause;
	data[i++] = nic->rx_fc_pause;
	data[i++] = nic->rx_fc_unsupported;
	data[i++] = nic->tx_tco_frames;
	data[i++] = nic->rx_tco_frames;
	data[i++] = nic->rx_short_frame_errors;
	data[i++] = nic->rx_over_length_errors;
}

static void e100_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, *e100_gstrings_test, sizeof(e100_gstrings_test));
		break;
	case ETH_SS_STATS:
		memcpy(data, *e100_gstrings_stats, sizeof(e100_gstrings_stats));
		break;
	}
}

static const struct ethtool_ops e100_ethtool_ops = {
	.get_settings		= e100_get_settings,
	.set_settings		= e100_set_settings,
	.get_drvinfo		= e100_get_drvinfo,
	.get_regs_len		= e100_get_regs_len,
	.get_regs		= e100_get_regs,
	.get_wol		= e100_get_wol,
	.set_wol		= e100_set_wol,
	.get_msglevel		= e100_get_msglevel,
	.set_msglevel		= e100_set_msglevel,
	.nway_reset		= e100_nway_reset,
	.get_link		= e100_get_link,
	.get_eeprom_len		= e100_get_eeprom_len,
	.get_eeprom		= e100_get_eeprom,
	.set_eeprom		= e100_set_eeprom,
	.get_ringparam		= e100_get_ringparam,
	.set_ringparam		= e100_set_ringparam,
	.self_test		= e100_diag_test,
	.get_strings		= e100_get_strings,
	.set_phys_id		= e100_set_phys_id,
	.get_ethtool_stats	= e100_get_ethtool_stats,
	.get_sset_count		= e100_get_sset_count,
	.get_ts_info		= ethtool_op_get_ts_info,
};

static int e100_do_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct nic *nic = netdev_priv(netdev);

	return generic_mii_ioctl(&nic->mii, if_mii(ifr), cmd, NULL);
}

static int e100_alloc(struct nic *nic)
{
	nic->mem = pci_alloc_consistent(nic->pdev, sizeof(struct mem),
		&nic->dma_addr);
	return nic->mem ? 0 : -ENOMEM;
}

static void e100_free(struct nic *nic)
{
	if (nic->mem) {
		pci_free_consistent(nic->pdev, sizeof(struct mem),
			nic->mem, nic->dma_addr);
		nic->mem = NULL;
	}
}

static int e100_open(struct net_device *netdev)
{
	struct nic *nic = netdev_priv(netdev);
	int err = 0;

	netif_carrier_off(netdev);
	if ((err = e100_up(nic)))
		netif_err(nic, ifup, nic->netdev, "Cannot open interface, aborting\n");
	return err;
}

static int e100_close(struct net_device *netdev)
{
	e100_down(netdev_priv(netdev));
	return 0;
}

static int e100_set_features(struct net_device *netdev,
			     netdev_features_t features)
{
	struct nic *nic = netdev_priv(netdev);
	netdev_features_t changed = features ^ netdev->features;

	if (!(changed & (NETIF_F_RXFCS | NETIF_F_RXALL)))
		return 0;

	netdev->features = features;
	e100_exec_cb(nic, NULL, e100_configure);
	return 0;
}

static const struct net_device_ops e100_netdev_ops = {
	.ndo_open		= e100_open,
	.ndo_stop		= e100_close,
	.ndo_start_xmit		= e100_xmit_frame,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= e100_set_multicast_list,
	.ndo_set_mac_address	= e100_set_mac_address,
	.ndo_change_mtu		= e100_change_mtu,
	.ndo_do_ioctl		= e100_do_ioctl,
	.ndo_tx_timeout		= e100_tx_timeout,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= e100_netpoll,
#endif
	.ndo_set_features	= e100_set_features,
};

static int e100_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct nic *nic;
	int err;

	if (!(netdev = alloc_etherdev(sizeof(struct nic))))
		return -ENOMEM;

	netdev->hw_features |= NETIF_F_RXFCS;
	netdev->priv_flags |= IFF_SUPP_NOFCS;
	netdev->hw_features |= NETIF_F_RXALL;

	netdev->netdev_ops = &e100_netdev_ops;
	netdev->ethtool_ops = &e100_ethtool_ops;
	netdev->watchdog_timeo = E100_WATCHDOG_PERIOD;
	strncpy(netdev->name, pci_name(pdev), sizeof(netdev->name) - 1);

	nic = netdev_priv(netdev);
	netif_napi_add(netdev, &nic->napi, e100_poll, E100_NAPI_WEIGHT);
	nic->netdev = netdev;
	nic->pdev = pdev;
	nic->msg_enable = (1 << debug) - 1;
	nic->mdio_ctrl = mdio_ctrl_hw;
	pci_set_drvdata(pdev, netdev);

	if ((err = pci_enable_device(pdev))) {
		netif_err(nic, probe, nic->netdev, "Cannot enable PCI device, aborting\n");
		goto err_out_free_dev;
	}

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		netif_err(nic, probe, nic->netdev, "Cannot find proper PCI device base address, aborting\n");
		err = -ENODEV;
		goto err_out_disable_pdev;
	}

	if ((err = pci_request_regions(pdev, DRV_NAME))) {
		netif_err(nic, probe, nic->netdev, "Cannot obtain PCI resources, aborting\n");
		goto err_out_disable_pdev;
	}

	if ((err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32)))) {
		netif_err(nic, probe, nic->netdev, "No usable DMA configuration, aborting\n");
		goto err_out_free_res;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	if (use_io)
		netif_info(nic, probe, nic->netdev, "using i/o access mode\n");

	nic->csr = pci_iomap(pdev, (use_io ? 1 : 0), sizeof(struct csr));
	if (!nic->csr) {
		netif_err(nic, probe, nic->netdev, "Cannot map device registers, aborting\n");
		err = -ENOMEM;
		goto err_out_free_res;
	}

	if (ent->driver_data)
		nic->flags |= ich;
	else
		nic->flags &= ~ich;

	e100_get_defaults(nic);

	/* D100 MAC doesn't allow rx of vlan packets with normal MTU */
	if (nic->mac < mac_82558_D101_A4)
		netdev->features |= NETIF_F_VLAN_CHALLENGED;

	/* locks must be initialized before calling hw_reset */
	spin_lock_init(&nic->cb_lock);
	spin_lock_init(&nic->cmd_lock);
	spin_lock_init(&nic->mdio_lock);

	/* Reset the device before pci_set_master() in case device is in some
	 * funky state and has an interrupt pending - hint: we don't have the
	 * interrupt handler registered yet. */
	e100_hw_reset(nic);

	pci_set_master(pdev);

	init_timer(&nic->watchdog);
	nic->watchdog.function = e100_watchdog;
	nic->watchdog.data = (unsigned long)nic;

	INIT_WORK(&nic->tx_timeout_task, e100_tx_timeout_task);

	if ((err = e100_alloc(nic))) {
		netif_err(nic, probe, nic->netdev, "Cannot alloc driver memory, aborting\n");
		goto err_out_iounmap;
	}

	if ((err = e100_eeprom_load(nic)))
		goto err_out_free;

	e100_phy_init(nic);

	memcpy(netdev->dev_addr, nic->eeprom, ETH_ALEN);
	if (!is_valid_ether_addr(netdev->dev_addr)) {
		if (!eeprom_bad_csum_allow) {
			netif_err(nic, probe, nic->netdev, "Invalid MAC address from EEPROM, aborting\n");
			err = -EAGAIN;
			goto err_out_free;
		} else {
			netif_err(nic, probe, nic->netdev, "Invalid MAC address from EEPROM, you MUST configure one.\n");
		}
	}

	/* Wol magic packet can be enabled from eeprom */
	if ((nic->mac >= mac_82558_D101_A4) &&
	   (nic->eeprom[eeprom_id] & eeprom_id_wol)) {
		nic->flags |= wol_magic;
		device_set_wakeup_enable(&pdev->dev, true);
	}

	/* ack any pending wake events, disable PME */
	pci_pme_active(pdev, false);

	strcpy(netdev->name, "eth%d");
	if ((err = register_netdev(netdev))) {
		netif_err(nic, probe, nic->netdev, "Cannot register net device, aborting\n");
		goto err_out_free;
	}
	nic->cbs_pool = pci_pool_create(netdev->name,
			   nic->pdev,
			   nic->params.cbs.max * sizeof(struct cb),
			   sizeof(u32),
			   0);
	netif_info(nic, probe, nic->netdev,
		   "addr 0x%llx, irq %d, MAC addr %pM\n",
		   (unsigned long long)pci_resource_start(pdev, use_io ? 1 : 0),
		   pdev->irq, netdev->dev_addr);

	return 0;

err_out_free:
	e100_free(nic);
err_out_iounmap:
	pci_iounmap(pdev, nic->csr);
err_out_free_res:
	pci_release_regions(pdev);
err_out_disable_pdev:
	pci_disable_device(pdev);
err_out_free_dev:
	free_netdev(netdev);
	return err;
}

static void e100_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);

	if (netdev) {
		struct nic *nic = netdev_priv(netdev);
		unregister_netdev(netdev);
		e100_free(nic);
		pci_iounmap(pdev, nic->csr);
		pci_pool_destroy(nic->cbs_pool);
		free_netdev(netdev);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
	}
}

#define E100_82552_SMARTSPEED   0x14   /* SmartSpeed Ctrl register */
#define E100_82552_REV_ANEG     0x0200 /* Reverse auto-negotiation */
#define E100_82552_ANEG_NOW     0x0400 /* Auto-negotiate now */
static void __e100_shutdown(struct pci_dev *pdev, bool *enable_wake)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct nic *nic = netdev_priv(netdev);

	if (netif_running(netdev))
		e100_down(nic);
	netif_device_detach(netdev);

	pci_save_state(pdev);

	if ((nic->flags & wol_magic) | e100_asf(nic)) {
		/* enable reverse auto-negotiation */
		if (nic->phy == phy_82552_v) {
			u16 smartspeed = mdio_read(netdev, nic->mii.phy_id,
			                           E100_82552_SMARTSPEED);

			mdio_write(netdev, nic->mii.phy_id,
			           E100_82552_SMARTSPEED, smartspeed |
			           E100_82552_REV_ANEG | E100_82552_ANEG_NOW);
		}
		*enable_wake = true;
	} else {
		*enable_wake = false;
	}

	pci_clear_master(pdev);
}

static int __e100_power_off(struct pci_dev *pdev, bool wake)
{
	if (wake)
		return pci_prepare_to_sleep(pdev);

	pci_wake_from_d3(pdev, false);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

#ifdef CONFIG_PM
static int e100_suspend(struct pci_dev *pdev, pm_message_t state)
{
	bool wake;
	__e100_shutdown(pdev, &wake);
	return __e100_power_off(pdev, wake);
}

static int e100_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct nic *nic = netdev_priv(netdev);

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	/* ack any pending wake events, disable PME */
	pci_enable_wake(pdev, PCI_D0, 0);

	/* disable reverse auto-negotiation */
	if (nic->phy == phy_82552_v) {
		u16 smartspeed = mdio_read(netdev, nic->mii.phy_id,
		                           E100_82552_SMARTSPEED);

		mdio_write(netdev, nic->mii.phy_id,
		           E100_82552_SMARTSPEED,
		           smartspeed & ~(E100_82552_REV_ANEG));
	}

	netif_device_attach(netdev);
	if (netif_running(netdev))
		e100_up(nic);

	return 0;
}
#endif /* CONFIG_PM */

static void e100_shutdown(struct pci_dev *pdev)
{
	bool wake;
	__e100_shutdown(pdev, &wake);
	if (system_state == SYSTEM_POWER_OFF)
		__e100_power_off(pdev, wake);
}

/* ------------------ PCI Error Recovery infrastructure  -------------- */
/**
 * e100_io_error_detected - called when PCI error is detected.
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
 */
static pci_ers_result_t e100_io_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct nic *nic = netdev_priv(netdev);

	netif_device_detach(netdev);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	if (netif_running(netdev))
		e100_down(nic);
	pci_disable_device(pdev);

	/* Request a slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * e100_io_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch.
 */
static pci_ers_result_t e100_io_slot_reset(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct nic *nic = netdev_priv(netdev);

	if (pci_enable_device(pdev)) {
		pr_err("Cannot re-enable PCI device after reset\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}
	pci_set_master(pdev);

	/* Only one device per card can do a reset */
	if (0 != PCI_FUNC(pdev->devfn))
		return PCI_ERS_RESULT_RECOVERED;
	e100_hw_reset(nic);
	e100_phy_init(nic);

	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * e100_io_resume - resume normal operations
 * @pdev: Pointer to PCI device
 *
 * Resume normal operations after an error recovery
 * sequence has been completed.
 */
static void e100_io_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct nic *nic = netdev_priv(netdev);

	/* ack any pending wake events, disable PME */
	pci_enable_wake(pdev, PCI_D0, 0);

	netif_device_attach(netdev);
	if (netif_running(netdev)) {
		e100_open(netdev);
		mod_timer(&nic->watchdog, jiffies);
	}
}

static const struct pci_error_handlers e100_err_handler = {
	.error_detected = e100_io_error_detected,
	.slot_reset = e100_io_slot_reset,
	.resume = e100_io_resume,
};

static struct pci_driver e100_driver = {
	.name =         DRV_NAME,
	.id_table =     e100_id_table,
	.probe =        e100_probe,
	.remove =       e100_remove,
#ifdef CONFIG_PM
	/* Power Management hooks */
	.suspend =      e100_suspend,
	.resume =       e100_resume,
#endif
	.shutdown =     e100_shutdown,
	.err_handler = &e100_err_handler,
};

static int __init e100_init_module(void)
{
	if (((1 << debug) - 1) & NETIF_MSG_DRV) {
		pr_info("%s, %s\n", DRV_DESCRIPTION, DRV_VERSION);
		pr_info("%s\n", DRV_COPYRIGHT);
	}
	return pci_register_driver(&e100_driver);
}

static void __exit e100_cleanup_module(void)
{
	pci_unregister_driver(&e100_driver);
}

module_init(e100_init_module);
module_exit(e100_cleanup_module);
