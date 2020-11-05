/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * QLogic qlcnic NIC Driver
 * Copyright (c) 2009-2013 QLogic Corporation
 */

#ifndef __QLCNIC_HW_H
#define __QLCNIC_HW_H

/* Common registers in 83xx and 82xx */
enum qlcnic_regs {
	QLCNIC_PEG_HALT_STATUS1 = 0,
	QLCNIC_PEG_HALT_STATUS2,
	QLCNIC_PEG_ALIVE_COUNTER,
	QLCNIC_FLASH_LOCK_OWNER,
	QLCNIC_FW_CAPABILITIES,
	QLCNIC_CRB_DRV_ACTIVE,
	QLCNIC_CRB_DEV_STATE,
	QLCNIC_CRB_DRV_STATE,
	QLCNIC_CRB_DRV_SCRATCH,
	QLCNIC_CRB_DEV_PARTITION_INFO,
	QLCNIC_CRB_DRV_IDC_VER,
	QLCNIC_FW_VERSION_MAJOR,
	QLCNIC_FW_VERSION_MINOR,
	QLCNIC_FW_VERSION_SUB,
	QLCNIC_CRB_DEV_NPAR_STATE,
	QLCNIC_FW_IMG_VALID,
	QLCNIC_CMDPEG_STATE,
	QLCNIC_RCVPEG_STATE,
	QLCNIC_ASIC_TEMP,
	QLCNIC_FW_API,
	QLCNIC_DRV_OP_MODE,
	QLCNIC_FLASH_LOCK,
	QLCNIC_FLASH_UNLOCK,
};

/* Read from an address offset from BAR0, existing registers */
#define QLC_SHARED_REG_RD32(a, addr)			\
	readl(((a)->ahw->pci_base0) + ((a)->ahw->reg_tbl[addr]))

/* Write to an address offset from BAR0, existing registers */
#define QLC_SHARED_REG_WR32(a, addr, value)		\
	writel(value, ((a)->ahw->pci_base0) + ((a)->ahw->reg_tbl[addr]))

/* Read from a direct address offset from BAR0, additional registers */
#define QLCRDX(ahw, addr)	\
	readl(((ahw)->pci_base0) + ((ahw)->ext_reg_tbl[addr]))

/* Write to a direct address offset from BAR0, additional registers */
#define QLCWRX(ahw, addr, value)	\
	writel(value, (((ahw)->pci_base0) + ((ahw)->ext_reg_tbl[addr])))

#define QLCNIC_CMD_CONFIGURE_IP_ADDR		0x1
#define QLCNIC_CMD_CONFIG_INTRPT		0x2
#define QLCNIC_CMD_CREATE_RX_CTX		0x7
#define QLCNIC_CMD_DESTROY_RX_CTX		0x8
#define QLCNIC_CMD_CREATE_TX_CTX		0x9
#define QLCNIC_CMD_DESTROY_TX_CTX		0xa
#define QLCNIC_CMD_CONFIGURE_LRO		0xC
#define QLCNIC_CMD_CONFIGURE_MAC_LEARNING	0xD
#define QLCNIC_CMD_GET_STATISTICS		0xF
#define QLCNIC_CMD_INTRPT_TEST			0x11
#define QLCNIC_CMD_SET_MTU			0x12
#define QLCNIC_CMD_READ_PHY			0x13
#define QLCNIC_CMD_WRITE_PHY			0x14
#define QLCNIC_CMD_READ_HW_REG			0x15
#define QLCNIC_CMD_GET_FLOW_CTL			0x16
#define QLCNIC_CMD_SET_FLOW_CTL			0x17
#define QLCNIC_CMD_READ_MAX_MTU			0x18
#define QLCNIC_CMD_READ_MAX_LRO			0x19
#define QLCNIC_CMD_MAC_ADDRESS			0x1f
#define QLCNIC_CMD_GET_PCI_INFO			0x20
#define QLCNIC_CMD_GET_NIC_INFO			0x21
#define QLCNIC_CMD_SET_NIC_INFO			0x22
#define QLCNIC_CMD_GET_ESWITCH_CAPABILITY	0x24
#define QLCNIC_CMD_TOGGLE_ESWITCH		0x25
#define QLCNIC_CMD_GET_ESWITCH_STATUS		0x26
#define QLCNIC_CMD_SET_PORTMIRRORING		0x27
#define QLCNIC_CMD_CONFIGURE_ESWITCH		0x28
#define QLCNIC_CMD_GET_ESWITCH_PORT_CONFIG	0x29
#define QLCNIC_CMD_GET_ESWITCH_STATS		0x2a
#define QLCNIC_CMD_CONFIG_PORT			0x2e
#define QLCNIC_CMD_TEMP_SIZE			0x2f
#define QLCNIC_CMD_GET_TEMP_HDR			0x30
#define QLCNIC_CMD_BC_EVENT_SETUP		0x31
#define	QLCNIC_CMD_CONFIG_VPORT			0x32
#define	QLCNIC_CMD_DCB_QUERY_CAP		0x34
#define	QLCNIC_CMD_DCB_QUERY_PARAM		0x35
#define QLCNIC_CMD_GET_MAC_STATS		0x37
#define QLCNIC_CMD_82XX_SET_DRV_VER		0x38
#define QLCNIC_CMD_MQ_TX_CONFIG_INTR		0x39
#define QLCNIC_CMD_GET_LED_STATUS		0x3C
#define QLCNIC_CMD_CONFIGURE_RSS		0x41
#define QLCNIC_CMD_CONFIG_INTR_COAL		0x43
#define QLCNIC_CMD_CONFIGURE_LED		0x44
#define QLCNIC_CMD_CONFIG_MAC_VLAN		0x45
#define QLCNIC_CMD_GET_LINK_EVENT		0x48
#define QLCNIC_CMD_CONFIGURE_MAC_RX_MODE	0x49
#define QLCNIC_CMD_CONFIGURE_HW_LRO		0x4A
#define QLCNIC_CMD_SET_INGRESS_ENCAP		0x4E
#define QLCNIC_CMD_INIT_NIC_FUNC		0x60
#define QLCNIC_CMD_STOP_NIC_FUNC		0x61
#define QLCNIC_CMD_IDC_ACK			0x63
#define QLCNIC_CMD_SET_PORT_CONFIG		0x66
#define QLCNIC_CMD_GET_PORT_CONFIG		0x67
#define QLCNIC_CMD_GET_LINK_STATUS		0x68
#define QLCNIC_CMD_SET_LED_CONFIG		0x69
#define QLCNIC_CMD_GET_LED_CONFIG		0x6A
#define QLCNIC_CMD_83XX_SET_DRV_VER		0x6F
#define QLCNIC_CMD_ADD_RCV_RINGS		0x0B
#define QLCNIC_CMD_83XX_EXTEND_ISCSI_DUMP_CAP	0x37

#define QLCNIC_INTRPT_INTX			1
#define QLCNIC_INTRPT_MSIX			3
#define QLCNIC_INTRPT_ADD			1
#define QLCNIC_INTRPT_DEL			2

#define QLCNIC_GET_CURRENT_MAC			1
#define QLCNIC_SET_STATION_MAC			2
#define QLCNIC_GET_DEFAULT_MAC			3
#define QLCNIC_GET_FAC_DEF_MAC			4
#define QLCNIC_SET_FAC_DEF_MAC			5

#define QLCNIC_MBX_LINK_EVENT		0x8001
#define QLCNIC_MBX_BC_EVENT		0x8002
#define QLCNIC_MBX_COMP_EVENT		0x8100
#define QLCNIC_MBX_REQUEST_EVENT	0x8101
#define QLCNIC_MBX_TIME_EXTEND_EVENT	0x8102
#define QLCNIC_MBX_DCBX_CONFIG_CHANGE_EVENT	0x8110
#define QLCNIC_MBX_SFP_INSERT_EVENT	0x8130
#define QLCNIC_MBX_SFP_REMOVE_EVENT	0x8131

struct qlcnic_mailbox_metadata {
	u32 cmd;
	u32 in_args;
	u32 out_args;
};

/* Mailbox ownership */
#define QLCNIC_GET_OWNER(val)	((val) & (BIT_0 | BIT_1))

#define QLCNIC_SET_OWNER        1
#define QLCNIC_CLR_OWNER        0
#define QLCNIC_MBX_TIMEOUT      5000

#define QLCNIC_MBX_RSP_OK	1
#define QLCNIC_MBX_PORT_RSP_OK	0x1a
#define QLCNIC_MBX_ASYNC_EVENT	BIT_15

/* Set HW Tx ring limit for 82xx adapter. */
#define QLCNIC_MAX_HW_TX_RINGS		8
#define QLCNIC_MAX_HW_VNIC_TX_RINGS	4
#define QLCNIC_MAX_TX_RINGS		8
#define QLCNIC_MAX_SDS_RINGS		8

struct qlcnic_pci_info;
struct qlcnic_info;
struct qlcnic_cmd_args;
struct ethtool_stats;
struct pci_device_id;
struct qlcnic_host_sds_ring;
struct qlcnic_host_tx_ring;
struct qlcnic_hardware_context;
struct qlcnic_adapter;
struct qlcnic_fw_dump;

int qlcnic_82xx_hw_read_wx_2M(struct qlcnic_adapter *adapter, ulong, int *);
int qlcnic_82xx_hw_write_wx_2M(struct qlcnic_adapter *, ulong, u32);
int qlcnic_82xx_config_hw_lro(struct qlcnic_adapter *adapter, int);
int qlcnic_82xx_nic_set_promisc(struct qlcnic_adapter *adapter, u32);
int qlcnic_82xx_napi_add(struct qlcnic_adapter *adapter,
			 struct net_device *netdev);
void qlcnic_82xx_get_beacon_state(struct qlcnic_adapter *);
void qlcnic_82xx_change_filter(struct qlcnic_adapter *adapter,
			       u64 *uaddr, u16 vlan_id,
			       struct qlcnic_host_tx_ring *tx_ring);
int qlcnic_82xx_config_intr_coalesce(struct qlcnic_adapter *,
				     struct ethtool_coalesce *);
int qlcnic_82xx_set_rx_coalesce(struct qlcnic_adapter *);
int qlcnic_82xx_config_rss(struct qlcnic_adapter *adapter, int);
void qlcnic_82xx_config_ipaddr(struct qlcnic_adapter *adapter,
			       __be32, int);
int qlcnic_82xx_linkevent_request(struct qlcnic_adapter *adapter, int);
void qlcnic_82xx_process_rcv_ring_diag(struct qlcnic_host_sds_ring *sds_ring);
int qlcnic_82xx_clear_lb_mode(struct qlcnic_adapter *adapter, u8);
int qlcnic_82xx_set_lb_mode(struct qlcnic_adapter *, u8);
void qlcnic_82xx_write_crb(struct qlcnic_adapter *, char *, loff_t, size_t);
void qlcnic_82xx_read_crb(struct qlcnic_adapter *, char *, loff_t, size_t);
int qlcnic_82xx_issue_cmd(struct qlcnic_adapter *adapter,
			  struct qlcnic_cmd_args *);
int qlcnic_82xx_mq_intrpt(struct qlcnic_adapter *, int);
int qlcnic_82xx_config_intrpt(struct qlcnic_adapter *, u8);
int qlcnic_82xx_fw_cmd_create_rx_ctx(struct qlcnic_adapter *);
int qlcnic_82xx_fw_cmd_create_tx_ctx(struct qlcnic_adapter *,
				     struct qlcnic_host_tx_ring *tx_ring, int);
void qlcnic_82xx_fw_cmd_del_rx_ctx(struct qlcnic_adapter *);
void qlcnic_82xx_fw_cmd_del_tx_ctx(struct qlcnic_adapter *,
				   struct qlcnic_host_tx_ring *);
int qlcnic_82xx_sre_macaddr_change(struct qlcnic_adapter *, u8 *, u16, u8);
int qlcnic_82xx_get_mac_address(struct qlcnic_adapter *, u8*, u8);
int qlcnic_82xx_get_nic_info(struct qlcnic_adapter *, struct qlcnic_info *, u8);
int qlcnic_82xx_set_nic_info(struct qlcnic_adapter *, struct qlcnic_info *);
int qlcnic_82xx_get_pci_info(struct qlcnic_adapter *, struct qlcnic_pci_info*);
int qlcnic_82xx_alloc_mbx_args(struct qlcnic_cmd_args *,
			       struct qlcnic_adapter *, u32);
int qlcnic_82xx_hw_write_wx_2M(struct qlcnic_adapter *, ulong, u32);
int qlcnic_82xx_get_board_info(struct qlcnic_adapter *);
int qlcnic_82xx_config_led(struct qlcnic_adapter *, u32, u32);
void qlcnic_82xx_get_func_no(struct qlcnic_adapter *);
int qlcnic_82xx_api_lock(struct qlcnic_adapter *);
void qlcnic_82xx_api_unlock(struct qlcnic_adapter *);
void qlcnic_82xx_napi_enable(struct qlcnic_adapter *);
void qlcnic_82xx_napi_disable(struct qlcnic_adapter *);
void qlcnic_82xx_napi_del(struct qlcnic_adapter *);
int qlcnic_82xx_shutdown(struct pci_dev *);
int qlcnic_82xx_resume(struct qlcnic_adapter *);
void qlcnic_clr_all_drv_state(struct qlcnic_adapter *adapter, u8 failed);
void qlcnic_fw_poll_work(struct work_struct *work);

u32 qlcnic_82xx_get_saved_state(void *, u32);
void qlcnic_82xx_set_saved_state(void *, u32, u32);
void qlcnic_82xx_cache_tmpl_hdr_values(struct qlcnic_fw_dump *);
u32 qlcnic_82xx_get_cap_size(void *, int);
void qlcnic_82xx_set_sys_info(void *, int, u32);
void qlcnic_82xx_store_cap_mask(void *, u32);
#endif				/* __QLCNIC_HW_H_ */
