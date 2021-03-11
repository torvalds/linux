/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright (c) 2016-2017 Hisilicon Limited.

#ifndef __HNAE3_H
#define __HNAE3_H

/* Names used in this framework:
 *      ae handle (handle):
 *        a set of queues provided by AE
 *      ring buffer queue (rbq):
 *        the channel between upper layer and the AE, can do tx and rx
 *      ring:
 *        a tx or rx channel within a rbq
 *      ring description (desc):
 *        an element in the ring with packet information
 *      buffer:
 *        a memory region referred by desc with the full packet payload
 *
 * "num" means a static number set as a parameter, "count" mean a dynamic
 *   number set while running
 * "cb" means control block
 */

#include <linux/acpi.h>
#include <linux/dcbnl.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/pkt_sched.h>
#include <linux/types.h>
#include <net/pkt_cls.h>

#define HNAE3_MOD_VERSION "1.0"

#define HNAE3_MIN_VECTOR_NUM	2 /* first one for misc, another for IO */

/* Device version */
#define HNAE3_DEVICE_VERSION_V1   0x00020
#define HNAE3_DEVICE_VERSION_V2   0x00021
#define HNAE3_DEVICE_VERSION_V3   0x00030

#define HNAE3_PCI_REVISION_BIT_SIZE		8

/* Device IDs */
#define HNAE3_DEV_ID_GE				0xA220
#define HNAE3_DEV_ID_25GE			0xA221
#define HNAE3_DEV_ID_25GE_RDMA			0xA222
#define HNAE3_DEV_ID_25GE_RDMA_MACSEC		0xA223
#define HNAE3_DEV_ID_50GE_RDMA			0xA224
#define HNAE3_DEV_ID_50GE_RDMA_MACSEC		0xA225
#define HNAE3_DEV_ID_100G_RDMA_MACSEC		0xA226
#define HNAE3_DEV_ID_200G_RDMA			0xA228
#define HNAE3_DEV_ID_VF				0xA22E
#define HNAE3_DEV_ID_RDMA_DCB_PFC_VF		0xA22F

#define HNAE3_CLASS_NAME_SIZE 16

#define HNAE3_DEV_INITED_B			0x0
#define HNAE3_DEV_SUPPORT_ROCE_B		0x1
#define HNAE3_DEV_SUPPORT_DCB_B			0x2
#define HNAE3_KNIC_CLIENT_INITED_B		0x3
#define HNAE3_UNIC_CLIENT_INITED_B		0x4
#define HNAE3_ROCE_CLIENT_INITED_B		0x5

#define HNAE3_DEV_SUPPORT_ROCE_DCB_BITS (BIT(HNAE3_DEV_SUPPORT_DCB_B) |\
		BIT(HNAE3_DEV_SUPPORT_ROCE_B))

#define hnae3_dev_roce_supported(hdev) \
	hnae3_get_bit((hdev)->ae_dev->flag, HNAE3_DEV_SUPPORT_ROCE_B)

#define hnae3_dev_dcb_supported(hdev) \
	hnae3_get_bit((hdev)->ae_dev->flag, HNAE3_DEV_SUPPORT_DCB_B)

enum HNAE3_DEV_CAP_BITS {
	HNAE3_DEV_SUPPORT_FD_B,
	HNAE3_DEV_SUPPORT_GRO_B,
	HNAE3_DEV_SUPPORT_FEC_B,
	HNAE3_DEV_SUPPORT_UDP_GSO_B,
	HNAE3_DEV_SUPPORT_QB_B,
	HNAE3_DEV_SUPPORT_FD_FORWARD_TC_B,
	HNAE3_DEV_SUPPORT_PTP_B,
	HNAE3_DEV_SUPPORT_INT_QL_B,
	HNAE3_DEV_SUPPORT_HW_TX_CSUM_B,
	HNAE3_DEV_SUPPORT_TX_PUSH_B,
	HNAE3_DEV_SUPPORT_PHY_IMP_B,
	HNAE3_DEV_SUPPORT_TQP_TXRX_INDEP_B,
	HNAE3_DEV_SUPPORT_HW_PAD_B,
	HNAE3_DEV_SUPPORT_STASH_B,
	HNAE3_DEV_SUPPORT_UDP_TUNNEL_CSUM_B,
	HNAE3_DEV_SUPPORT_PAUSE_B,
};

#define hnae3_dev_fd_supported(hdev) \
	test_bit(HNAE3_DEV_SUPPORT_FD_B, (hdev)->ae_dev->caps)

#define hnae3_dev_gro_supported(hdev) \
	test_bit(HNAE3_DEV_SUPPORT_GRO_B, (hdev)->ae_dev->caps)

#define hnae3_dev_fec_supported(hdev) \
	test_bit(HNAE3_DEV_SUPPORT_FEC_B, (hdev)->ae_dev->caps)

#define hnae3_dev_udp_gso_supported(hdev) \
	test_bit(HNAE3_DEV_SUPPORT_UDP_GSO_B, (hdev)->ae_dev->caps)

#define hnae3_dev_qb_supported(hdev) \
	test_bit(HNAE3_DEV_SUPPORT_QB_B, (hdev)->ae_dev->caps)

#define hnae3_dev_fd_forward_tc_supported(hdev) \
	test_bit(HNAE3_DEV_SUPPORT_FD_FORWARD_TC_B, (hdev)->ae_dev->caps)

#define hnae3_dev_ptp_supported(hdev) \
	test_bit(HNAE3_DEV_SUPPORT_PTP_B, (hdev)->ae_dev->caps)

#define hnae3_dev_int_ql_supported(hdev) \
	test_bit(HNAE3_DEV_SUPPORT_INT_QL_B, (hdev)->ae_dev->caps)

#define hnae3_dev_hw_csum_supported(hdev) \
	test_bit(HNAE3_DEV_SUPPORT_HW_TX_CSUM_B, (hdev)->ae_dev->caps)

#define hnae3_dev_tx_push_supported(hdev) \
	test_bit(HNAE3_DEV_SUPPORT_TX_PUSH_B, (hdev)->ae_dev->caps)

#define hnae3_dev_phy_imp_supported(hdev) \
	test_bit(HNAE3_DEV_SUPPORT_PHY_IMP_B, (hdev)->ae_dev->caps)

#define hnae3_dev_tqp_txrx_indep_supported(hdev) \
	test_bit(HNAE3_DEV_SUPPORT_TQP_TXRX_INDEP_B, (hdev)->ae_dev->caps)

#define hnae3_dev_hw_pad_supported(hdev) \
	test_bit(HNAE3_DEV_SUPPORT_HW_PAD_B, (hdev)->ae_dev->caps)

#define hnae3_dev_stash_supported(hdev) \
	test_bit(HNAE3_DEV_SUPPORT_STASH_B, (hdev)->ae_dev->caps)

#define hnae3_dev_pause_supported(hdev) \
	test_bit(HNAE3_DEV_SUPPORT_PAUSE_B, (hdev)->ae_dev->caps)

#define hnae3_ae_dev_tqp_txrx_indep_supported(ae_dev) \
	test_bit(HNAE3_DEV_SUPPORT_TQP_TXRX_INDEP_B, (ae_dev)->caps)

#define ring_ptr_move_fw(ring, p) \
	((ring)->p = ((ring)->p + 1) % (ring)->desc_num)
#define ring_ptr_move_bw(ring, p) \
	((ring)->p = ((ring)->p - 1 + (ring)->desc_num) % (ring)->desc_num)

enum hns_desc_type {
	DESC_TYPE_UNKNOWN,
	DESC_TYPE_SKB,
	DESC_TYPE_FRAGLIST_SKB,
	DESC_TYPE_PAGE,
};

struct hnae3_handle;

struct hnae3_queue {
	void __iomem *io_base;
	struct hnae3_ae_algo *ae_algo;
	struct hnae3_handle *handle;
	int tqp_index;		/* index in a handle */
	u32 buf_size;		/* size for hnae_desc->addr, preset by AE */
	u16 tx_desc_num;	/* total number of tx desc */
	u16 rx_desc_num;	/* total number of rx desc */
};

struct hns3_mac_stats {
	u64 tx_pause_cnt;
	u64 rx_pause_cnt;
};

/* hnae3 loop mode */
enum hnae3_loop {
	HNAE3_LOOP_APP,
	HNAE3_LOOP_SERIAL_SERDES,
	HNAE3_LOOP_PARALLEL_SERDES,
	HNAE3_LOOP_PHY,
	HNAE3_LOOP_NONE,
};

enum hnae3_client_type {
	HNAE3_CLIENT_KNIC,
	HNAE3_CLIENT_ROCE,
};

/* mac media type */
enum hnae3_media_type {
	HNAE3_MEDIA_TYPE_UNKNOWN,
	HNAE3_MEDIA_TYPE_FIBER,
	HNAE3_MEDIA_TYPE_COPPER,
	HNAE3_MEDIA_TYPE_BACKPLANE,
	HNAE3_MEDIA_TYPE_NONE,
};

/* must be consistent with definition in firmware */
enum hnae3_module_type {
	HNAE3_MODULE_TYPE_UNKNOWN	= 0x00,
	HNAE3_MODULE_TYPE_FIBRE_LR	= 0x01,
	HNAE3_MODULE_TYPE_FIBRE_SR	= 0x02,
	HNAE3_MODULE_TYPE_AOC		= 0x03,
	HNAE3_MODULE_TYPE_CR		= 0x04,
	HNAE3_MODULE_TYPE_KR		= 0x05,
	HNAE3_MODULE_TYPE_TP		= 0x06,
};

enum hnae3_fec_mode {
	HNAE3_FEC_AUTO = 0,
	HNAE3_FEC_BASER,
	HNAE3_FEC_RS,
	HNAE3_FEC_USER_DEF,
};

enum hnae3_reset_notify_type {
	HNAE3_UP_CLIENT,
	HNAE3_DOWN_CLIENT,
	HNAE3_INIT_CLIENT,
	HNAE3_UNINIT_CLIENT,
};

enum hnae3_hw_error_type {
	HNAE3_PPU_POISON_ERROR,
	HNAE3_CMDQ_ECC_ERROR,
	HNAE3_IMP_RD_POISON_ERROR,
	HNAE3_ROCEE_AXI_RESP_ERROR,
};

enum hnae3_reset_type {
	HNAE3_VF_RESET,
	HNAE3_VF_FUNC_RESET,
	HNAE3_VF_PF_FUNC_RESET,
	HNAE3_VF_FULL_RESET,
	HNAE3_FLR_RESET,
	HNAE3_FUNC_RESET,
	HNAE3_GLOBAL_RESET,
	HNAE3_IMP_RESET,
	HNAE3_UNKNOWN_RESET,
	HNAE3_NONE_RESET,
	HNAE3_MAX_RESET,
};

enum hnae3_port_base_vlan_state {
	HNAE3_PORT_BASE_VLAN_DISABLE,
	HNAE3_PORT_BASE_VLAN_ENABLE,
	HNAE3_PORT_BASE_VLAN_MODIFY,
	HNAE3_PORT_BASE_VLAN_NOCHANGE,
};

struct hnae3_vector_info {
	u8 __iomem *io_addr;
	int vector;
};

#define HNAE3_RING_TYPE_B 0
#define HNAE3_RING_TYPE_TX 0
#define HNAE3_RING_TYPE_RX 1
#define HNAE3_RING_GL_IDX_S 0
#define HNAE3_RING_GL_IDX_M GENMASK(1, 0)
#define HNAE3_RING_GL_RX 0
#define HNAE3_RING_GL_TX 1

#define HNAE3_FW_VERSION_BYTE3_SHIFT	24
#define HNAE3_FW_VERSION_BYTE3_MASK	GENMASK(31, 24)
#define HNAE3_FW_VERSION_BYTE2_SHIFT	16
#define HNAE3_FW_VERSION_BYTE2_MASK	GENMASK(23, 16)
#define HNAE3_FW_VERSION_BYTE1_SHIFT	8
#define HNAE3_FW_VERSION_BYTE1_MASK	GENMASK(15, 8)
#define HNAE3_FW_VERSION_BYTE0_SHIFT	0
#define HNAE3_FW_VERSION_BYTE0_MASK	GENMASK(7, 0)

struct hnae3_ring_chain_node {
	struct hnae3_ring_chain_node *next;
	u32 tqp_index;
	u32 flag;
	u32 int_gl_idx;
};

#define HNAE3_IS_TX_RING(node) \
	(((node)->flag & 1 << HNAE3_RING_TYPE_B) == HNAE3_RING_TYPE_TX)

/* device specification info from firmware */
struct hnae3_dev_specs {
	u32 mac_entry_num; /* number of mac-vlan table entry */
	u32 mng_entry_num; /* number of manager table entry */
	u32 max_tm_rate;
	u16 rss_ind_tbl_size;
	u16 rss_key_size;
	u16 int_ql_max; /* max value of interrupt coalesce based on INT_QL */
	u16 max_int_gl; /* max value of interrupt coalesce based on INT_GL */
	u8 max_non_tso_bd_num; /* max BD number of one non-TSO packet */
	u16 max_frm_size;
	u16 max_qset_num;
};

struct hnae3_client_ops {
	int (*init_instance)(struct hnae3_handle *handle);
	void (*uninit_instance)(struct hnae3_handle *handle, bool reset);
	void (*link_status_change)(struct hnae3_handle *handle, bool state);
	int (*reset_notify)(struct hnae3_handle *handle,
			    enum hnae3_reset_notify_type type);
	void (*process_hw_error)(struct hnae3_handle *handle,
				 enum hnae3_hw_error_type);
};

#define HNAE3_CLIENT_NAME_LENGTH 16
struct hnae3_client {
	char name[HNAE3_CLIENT_NAME_LENGTH];
	unsigned long state;
	enum hnae3_client_type type;
	const struct hnae3_client_ops *ops;
	struct list_head node;
};

#define HNAE3_DEV_CAPS_MAX_NUM	96
struct hnae3_ae_dev {
	struct pci_dev *pdev;
	const struct hnae3_ae_ops *ops;
	struct list_head node;
	u32 flag;
	unsigned long hw_err_reset_req;
	struct hnae3_dev_specs dev_specs;
	u32 dev_version;
	unsigned long caps[BITS_TO_LONGS(HNAE3_DEV_CAPS_MAX_NUM)];
	void *priv;
};

/* This struct defines the operation on the handle.
 *
 * init_ae_dev(): (mandatory)
 *   Get PF configure from pci_dev and initialize PF hardware
 * uninit_ae_dev()
 *   Disable PF device and release PF resource
 * register_client
 *   Register client to ae_dev
 * unregister_client()
 *   Unregister client from ae_dev
 * start()
 *   Enable the hardware
 * stop()
 *   Disable the hardware
 * start_client()
 *   Inform the hclge that client has been started
 * stop_client()
 *   Inform the hclge that client has been stopped
 * get_status()
 *   Get the carrier state of the back channel of the handle, 1 for ok, 0 for
 *   non-ok
 * get_ksettings_an_result()
 *   Get negotiation status,speed and duplex
 * get_media_type()
 *   Get media type of MAC
 * check_port_speed()
 *   Check target speed whether is supported
 * adjust_link()
 *   Adjust link status
 * set_loopback()
 *   Set loopback
 * set_promisc_mode
 *   Set promisc mode
 * request_update_promisc_mode
 *   request to hclge(vf) to update promisc mode
 * set_mtu()
 *   set mtu
 * get_pauseparam()
 *   get tx and rx of pause frame use
 * set_pauseparam()
 *   set tx and rx of pause frame use
 * set_autoneg()
 *   set auto autonegotiation of pause frame use
 * get_autoneg()
 *   get auto autonegotiation of pause frame use
 * restart_autoneg()
 *   restart autonegotiation
 * halt_autoneg()
 *   halt/resume autonegotiation when autonegotiation on
 * get_coalesce_usecs()
 *   get usecs to delay a TX interrupt after a packet is sent
 * get_rx_max_coalesced_frames()
 *   get Maximum number of packets to be sent before a TX interrupt.
 * set_coalesce_usecs()
 *   set usecs to delay a TX interrupt after a packet is sent
 * set_coalesce_frames()
 *   set Maximum number of packets to be sent before a TX interrupt.
 * get_mac_addr()
 *   get mac address
 * set_mac_addr()
 *   set mac address
 * add_uc_addr
 *   Add unicast addr to mac table
 * rm_uc_addr
 *   Remove unicast addr from mac table
 * set_mc_addr()
 *   Set multicast address
 * add_mc_addr
 *   Add multicast address to mac table
 * rm_mc_addr
 *   Remove multicast address from mac table
 * update_stats()
 *   Update Old network device statistics
 * get_mac_stats()
 *   get mac pause statistics including tx_cnt and rx_cnt
 * get_ethtool_stats()
 *   Get ethtool network device statistics
 * get_strings()
 *   Get a set of strings that describe the requested objects
 * get_sset_count()
 *   Get number of strings that @get_strings will write
 * update_led_status()
 *   Update the led status
 * set_led_id()
 *   Set led id
 * get_regs()
 *   Get regs dump
 * get_regs_len()
 *   Get the len of the regs dump
 * get_rss_key_size()
 *   Get rss key size
 * get_rss()
 *   Get rss table
 * set_rss()
 *   Set rss table
 * get_tc_size()
 *   Get tc size of handle
 * get_vector()
 *   Get vector number and vector information
 * put_vector()
 *   Put the vector in hdev
 * map_ring_to_vector()
 *   Map rings to vector
 * unmap_ring_from_vector()
 *   Unmap rings from vector
 * reset_queue()
 *   Reset queue
 * get_fw_version()
 *   Get firmware version
 * get_mdix_mode()
 *   Get media typr of phy
 * enable_vlan_filter()
 *   Enable vlan filter
 * set_vlan_filter()
 *   Set vlan filter config of Ports
 * set_vf_vlan_filter()
 *   Set vlan filter config of vf
 * enable_hw_strip_rxvtag()
 *   Enable/disable hardware strip vlan tag of packets received
 * set_gro_en
 *   Enable/disable HW GRO
 * add_arfs_entry
 *   Check the 5-tuples of flow, and create flow director rule
 * get_vf_config
 *   Get the VF configuration setting by the host
 * set_vf_link_state
 *   Set VF link status
 * set_vf_spoofchk
 *   Enable/disable spoof check for specified vf
 * set_vf_trust
 *   Enable/disable trust for specified vf, if the vf being trusted, then
 *   it can enable promisc mode
 * set_vf_rate
 *   Set the max tx rate of specified vf.
 * set_vf_mac
 *   Configure the default MAC for specified VF
 * get_module_eeprom
 *   Get the optical module eeprom info.
 * add_cls_flower
 *   Add clsflower rule
 * del_cls_flower
 *   Delete clsflower rule
 * cls_flower_active
 *   Check if any cls flower rule exist
 * dbg_read_cmd
 *   Execute debugfs read command.
 */
struct hnae3_ae_ops {
	int (*init_ae_dev)(struct hnae3_ae_dev *ae_dev);
	void (*uninit_ae_dev)(struct hnae3_ae_dev *ae_dev);
	void (*flr_prepare)(struct hnae3_ae_dev *ae_dev);
	void (*flr_done)(struct hnae3_ae_dev *ae_dev);
	int (*init_client_instance)(struct hnae3_client *client,
				    struct hnae3_ae_dev *ae_dev);
	void (*uninit_client_instance)(struct hnae3_client *client,
				       struct hnae3_ae_dev *ae_dev);
	int (*start)(struct hnae3_handle *handle);
	void (*stop)(struct hnae3_handle *handle);
	int (*client_start)(struct hnae3_handle *handle);
	void (*client_stop)(struct hnae3_handle *handle);
	int (*get_status)(struct hnae3_handle *handle);
	void (*get_ksettings_an_result)(struct hnae3_handle *handle,
					u8 *auto_neg, u32 *speed, u8 *duplex);

	int (*cfg_mac_speed_dup_h)(struct hnae3_handle *handle, int speed,
				   u8 duplex);

	void (*get_media_type)(struct hnae3_handle *handle, u8 *media_type,
			       u8 *module_type);
	int (*check_port_speed)(struct hnae3_handle *handle, u32 speed);
	void (*get_fec)(struct hnae3_handle *handle, u8 *fec_ability,
			u8 *fec_mode);
	int (*set_fec)(struct hnae3_handle *handle, u32 fec_mode);
	void (*adjust_link)(struct hnae3_handle *handle, int speed, int duplex);
	int (*set_loopback)(struct hnae3_handle *handle,
			    enum hnae3_loop loop_mode, bool en);

	int (*set_promisc_mode)(struct hnae3_handle *handle, bool en_uc_pmc,
				bool en_mc_pmc);
	void (*request_update_promisc_mode)(struct hnae3_handle *handle);
	int (*set_mtu)(struct hnae3_handle *handle, int new_mtu);

	void (*get_pauseparam)(struct hnae3_handle *handle,
			       u32 *auto_neg, u32 *rx_en, u32 *tx_en);
	int (*set_pauseparam)(struct hnae3_handle *handle,
			      u32 auto_neg, u32 rx_en, u32 tx_en);

	int (*set_autoneg)(struct hnae3_handle *handle, bool enable);
	int (*get_autoneg)(struct hnae3_handle *handle);
	int (*restart_autoneg)(struct hnae3_handle *handle);
	int (*halt_autoneg)(struct hnae3_handle *handle, bool halt);

	void (*get_coalesce_usecs)(struct hnae3_handle *handle,
				   u32 *tx_usecs, u32 *rx_usecs);
	void (*get_rx_max_coalesced_frames)(struct hnae3_handle *handle,
					    u32 *tx_frames, u32 *rx_frames);
	int (*set_coalesce_usecs)(struct hnae3_handle *handle, u32 timeout);
	int (*set_coalesce_frames)(struct hnae3_handle *handle,
				   u32 coalesce_frames);
	void (*get_coalesce_range)(struct hnae3_handle *handle,
				   u32 *tx_frames_low, u32 *rx_frames_low,
				   u32 *tx_frames_high, u32 *rx_frames_high,
				   u32 *tx_usecs_low, u32 *rx_usecs_low,
				   u32 *tx_usecs_high, u32 *rx_usecs_high);

	void (*get_mac_addr)(struct hnae3_handle *handle, u8 *p);
	int (*set_mac_addr)(struct hnae3_handle *handle, void *p,
			    bool is_first);
	int (*do_ioctl)(struct hnae3_handle *handle,
			struct ifreq *ifr, int cmd);
	int (*add_uc_addr)(struct hnae3_handle *handle,
			   const unsigned char *addr);
	int (*rm_uc_addr)(struct hnae3_handle *handle,
			  const unsigned char *addr);
	int (*set_mc_addr)(struct hnae3_handle *handle, void *addr);
	int (*add_mc_addr)(struct hnae3_handle *handle,
			   const unsigned char *addr);
	int (*rm_mc_addr)(struct hnae3_handle *handle,
			  const unsigned char *addr);
	void (*set_tso_stats)(struct hnae3_handle *handle, int enable);
	void (*update_stats)(struct hnae3_handle *handle,
			     struct net_device_stats *net_stats);
	void (*get_stats)(struct hnae3_handle *handle, u64 *data);
	void (*get_mac_stats)(struct hnae3_handle *handle,
			      struct hns3_mac_stats *mac_stats);
	void (*get_strings)(struct hnae3_handle *handle,
			    u32 stringset, u8 *data);
	int (*get_sset_count)(struct hnae3_handle *handle, int stringset);

	void (*get_regs)(struct hnae3_handle *handle, u32 *version,
			 void *data);
	int (*get_regs_len)(struct hnae3_handle *handle);

	u32 (*get_rss_key_size)(struct hnae3_handle *handle);
	int (*get_rss)(struct hnae3_handle *handle, u32 *indir, u8 *key,
		       u8 *hfunc);
	int (*set_rss)(struct hnae3_handle *handle, const u32 *indir,
		       const u8 *key, const u8 hfunc);
	int (*set_rss_tuple)(struct hnae3_handle *handle,
			     struct ethtool_rxnfc *cmd);
	int (*get_rss_tuple)(struct hnae3_handle *handle,
			     struct ethtool_rxnfc *cmd);

	int (*get_tc_size)(struct hnae3_handle *handle);

	int (*get_vector)(struct hnae3_handle *handle, u16 vector_num,
			  struct hnae3_vector_info *vector_info);
	int (*put_vector)(struct hnae3_handle *handle, int vector_num);
	int (*map_ring_to_vector)(struct hnae3_handle *handle,
				  int vector_num,
				  struct hnae3_ring_chain_node *vr_chain);
	int (*unmap_ring_from_vector)(struct hnae3_handle *handle,
				      int vector_num,
				      struct hnae3_ring_chain_node *vr_chain);

	int (*reset_queue)(struct hnae3_handle *handle, u16 queue_id);
	u32 (*get_fw_version)(struct hnae3_handle *handle);
	void (*get_mdix_mode)(struct hnae3_handle *handle,
			      u8 *tp_mdix_ctrl, u8 *tp_mdix);

	void (*enable_vlan_filter)(struct hnae3_handle *handle, bool enable);
	int (*set_vlan_filter)(struct hnae3_handle *handle, __be16 proto,
			       u16 vlan_id, bool is_kill);
	int (*set_vf_vlan_filter)(struct hnae3_handle *handle, int vfid,
				  u16 vlan, u8 qos, __be16 proto);
	int (*enable_hw_strip_rxvtag)(struct hnae3_handle *handle, bool enable);
	void (*reset_event)(struct pci_dev *pdev, struct hnae3_handle *handle);
	enum hnae3_reset_type (*get_reset_level)(struct hnae3_ae_dev *ae_dev,
						 unsigned long *addr);
	void (*set_default_reset_request)(struct hnae3_ae_dev *ae_dev,
					  enum hnae3_reset_type rst_type);
	void (*get_channels)(struct hnae3_handle *handle,
			     struct ethtool_channels *ch);
	void (*get_tqps_and_rss_info)(struct hnae3_handle *h,
				      u16 *alloc_tqps, u16 *max_rss_size);
	int (*set_channels)(struct hnae3_handle *handle, u32 new_tqps_num,
			    bool rxfh_configured);
	void (*get_flowctrl_adv)(struct hnae3_handle *handle,
				 u32 *flowctrl_adv);
	int (*set_led_id)(struct hnae3_handle *handle,
			  enum ethtool_phys_id_state status);
	void (*get_link_mode)(struct hnae3_handle *handle,
			      unsigned long *supported,
			      unsigned long *advertising);
	int (*add_fd_entry)(struct hnae3_handle *handle,
			    struct ethtool_rxnfc *cmd);
	int (*del_fd_entry)(struct hnae3_handle *handle,
			    struct ethtool_rxnfc *cmd);
	void (*del_all_fd_entries)(struct hnae3_handle *handle,
				   bool clear_list);
	int (*get_fd_rule_cnt)(struct hnae3_handle *handle,
			       struct ethtool_rxnfc *cmd);
	int (*get_fd_rule_info)(struct hnae3_handle *handle,
				struct ethtool_rxnfc *cmd);
	int (*get_fd_all_rules)(struct hnae3_handle *handle,
				struct ethtool_rxnfc *cmd, u32 *rule_locs);
	void (*enable_fd)(struct hnae3_handle *handle, bool enable);
	int (*add_arfs_entry)(struct hnae3_handle *handle, u16 queue_id,
			      u16 flow_id, struct flow_keys *fkeys);
	int (*dbg_run_cmd)(struct hnae3_handle *handle, const char *cmd_buf);
	int (*dbg_read_cmd)(struct hnae3_handle *handle, const char *cmd_buf,
			    char *buf, int len);
	pci_ers_result_t (*handle_hw_ras_error)(struct hnae3_ae_dev *ae_dev);
	bool (*get_hw_reset_stat)(struct hnae3_handle *handle);
	bool (*ae_dev_resetting)(struct hnae3_handle *handle);
	unsigned long (*ae_dev_reset_cnt)(struct hnae3_handle *handle);
	int (*set_gro_en)(struct hnae3_handle *handle, bool enable);
	u16 (*get_global_queue_id)(struct hnae3_handle *handle, u16 queue_id);
	void (*set_timer_task)(struct hnae3_handle *handle, bool enable);
	int (*mac_connect_phy)(struct hnae3_handle *handle);
	void (*mac_disconnect_phy)(struct hnae3_handle *handle);
	int (*get_vf_config)(struct hnae3_handle *handle, int vf,
			     struct ifla_vf_info *ivf);
	int (*set_vf_link_state)(struct hnae3_handle *handle, int vf,
				 int link_state);
	int (*set_vf_spoofchk)(struct hnae3_handle *handle, int vf,
			       bool enable);
	int (*set_vf_trust)(struct hnae3_handle *handle, int vf, bool enable);
	int (*set_vf_rate)(struct hnae3_handle *handle, int vf,
			   int min_tx_rate, int max_tx_rate, bool force);
	int (*set_vf_mac)(struct hnae3_handle *handle, int vf, u8 *p);
	int (*get_module_eeprom)(struct hnae3_handle *handle, u32 offset,
				 u32 len, u8 *data);
	bool (*get_cmdq_stat)(struct hnae3_handle *handle);
	int (*add_cls_flower)(struct hnae3_handle *handle,
			      struct flow_cls_offload *cls_flower, int tc);
	int (*del_cls_flower)(struct hnae3_handle *handle,
			      struct flow_cls_offload *cls_flower);
	bool (*cls_flower_active)(struct hnae3_handle *handle);
};

struct hnae3_dcb_ops {
	/* IEEE 802.1Qaz std */
	int (*ieee_getets)(struct hnae3_handle *, struct ieee_ets *);
	int (*ieee_setets)(struct hnae3_handle *, struct ieee_ets *);
	int (*ieee_getpfc)(struct hnae3_handle *, struct ieee_pfc *);
	int (*ieee_setpfc)(struct hnae3_handle *, struct ieee_pfc *);

	/* DCBX configuration */
	u8   (*getdcbx)(struct hnae3_handle *);
	u8   (*setdcbx)(struct hnae3_handle *, u8);

	int (*setup_tc)(struct hnae3_handle *handle,
			struct tc_mqprio_qopt_offload *mqprio_qopt);
};

struct hnae3_ae_algo {
	const struct hnae3_ae_ops *ops;
	struct list_head node;
	const struct pci_device_id *pdev_id_table;
};

#define HNAE3_INT_NAME_LEN        32
#define HNAE3_ITR_COUNTDOWN_START 100

#define HNAE3_MAX_TC		8
#define HNAE3_MAX_USER_PRIO	8
struct hnae3_tc_info {
	u8 prio_tc[HNAE3_MAX_USER_PRIO]; /* TC indexed by prio */
	u16 tqp_count[HNAE3_MAX_TC];
	u16 tqp_offset[HNAE3_MAX_TC];
	unsigned long tc_en; /* bitmap of TC enabled */
	u8 num_tc; /* Total number of enabled TCs */
	bool mqprio_active;
};

struct hnae3_knic_private_info {
	struct net_device *netdev; /* Set by KNIC client when init instance */
	u16 rss_size;		   /* Allocated RSS queues */
	u16 req_rss_size;
	u16 rx_buf_len;
	u16 num_tx_desc;
	u16 num_rx_desc;

	struct hnae3_tc_info tc_info;

	u16 num_tqps;		  /* total number of TQPs in this handle */
	struct hnae3_queue **tqp;  /* array base of all TQPs in this instance */
	const struct hnae3_dcb_ops *dcb_ops;

	u16 int_rl_setting;
	enum pkt_hash_types rss_type;
};

struct hnae3_roce_private_info {
	struct net_device *netdev;
	void __iomem *roce_io_base;
	void __iomem *roce_mem_base;
	int base_vector;
	int num_vectors;

	/* The below attributes defined for RoCE client, hnae3 gives
	 * initial values to them, and RoCE client can modify and use
	 * them.
	 */
	unsigned long reset_state;
	unsigned long instance_state;
	unsigned long state;
};

#define HNAE3_SUPPORT_APP_LOOPBACK    BIT(0)
#define HNAE3_SUPPORT_PHY_LOOPBACK    BIT(1)
#define HNAE3_SUPPORT_SERDES_SERIAL_LOOPBACK	BIT(2)
#define HNAE3_SUPPORT_VF	      BIT(3)
#define HNAE3_SUPPORT_SERDES_PARALLEL_LOOPBACK	BIT(4)

#define HNAE3_USER_UPE		BIT(0)	/* unicast promisc enabled by user */
#define HNAE3_USER_MPE		BIT(1)	/* mulitcast promisc enabled by user */
#define HNAE3_BPE		BIT(2)	/* broadcast promisc enable */
#define HNAE3_OVERFLOW_UPE	BIT(3)	/* unicast mac vlan overflow */
#define HNAE3_OVERFLOW_MPE	BIT(4)	/* multicast mac vlan overflow */
#define HNAE3_VLAN_FLTR		BIT(5)	/* enable vlan filter */
#define HNAE3_UPE		(HNAE3_USER_UPE | HNAE3_OVERFLOW_UPE)
#define HNAE3_MPE		(HNAE3_USER_MPE | HNAE3_OVERFLOW_MPE)

enum hnae3_pflag {
	HNAE3_PFLAG_LIMIT_PROMISC,
	HNAE3_PFLAG_MAX
};

struct hnae3_handle {
	struct hnae3_client *client;
	struct pci_dev *pdev;
	void *priv;
	struct hnae3_ae_algo *ae_algo;  /* the class who provides this handle */
	u64 flags; /* Indicate the capabilities for this handle */

	union {
		struct net_device *netdev; /* first member */
		struct hnae3_knic_private_info kinfo;
		struct hnae3_roce_private_info rinfo;
	};

	u32 numa_node_mask;	/* for multi-chip support */

	enum hnae3_port_base_vlan_state port_base_vlan_state;

	u8 netdev_flags;
	struct dentry *hnae3_dbgfs;

	/* Network interface message level enabled bits */
	u32 msg_enable;

	unsigned long supported_pflags;
	unsigned long priv_flags;
};

#define hnae3_set_field(origin, mask, shift, val) \
	do { \
		(origin) &= (~(mask)); \
		(origin) |= ((val) << (shift)) & (mask); \
	} while (0)
#define hnae3_get_field(origin, mask, shift) (((origin) & (mask)) >> (shift))

#define hnae3_set_bit(origin, shift, val) \
	hnae3_set_field(origin, 0x1 << (shift), shift, val)
#define hnae3_get_bit(origin, shift) \
	hnae3_get_field(origin, 0x1 << (shift), shift)

#define HNAE3_DBG_TM_NODES		"tm_nodes"
#define HNAE3_DBG_TM_PRI		"tm_priority"
#define HNAE3_DBG_TM_QSET		"tm_qset"

int hnae3_register_ae_dev(struct hnae3_ae_dev *ae_dev);
void hnae3_unregister_ae_dev(struct hnae3_ae_dev *ae_dev);

void hnae3_unregister_ae_algo(struct hnae3_ae_algo *ae_algo);
void hnae3_register_ae_algo(struct hnae3_ae_algo *ae_algo);

void hnae3_unregister_client(struct hnae3_client *client);
int hnae3_register_client(struct hnae3_client *client);

void hnae3_set_client_init_flag(struct hnae3_client *client,
				struct hnae3_ae_dev *ae_dev,
				unsigned int inited);
#endif
