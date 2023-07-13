/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#ifndef _WX_HW_H_
#define _WX_HW_H_

void wx_intr_enable(struct wx *wx, u64 qmask);
void wx_irq_disable(struct wx *wx);
int wx_check_flash_load(struct wx *wx, u32 check_bit);
void wx_control_hw(struct wx *wx, bool drv);
int wx_mng_present(struct wx *wx);
int wx_host_interface_command(struct wx *wx, u32 *buffer,
			      u32 length, u32 timeout, bool return_data);
int wx_read_ee_hostif(struct wx *wx, u16 offset, u16 *data);
int wx_read_ee_hostif_buffer(struct wx *wx,
			     u16 offset, u16 words, u16 *data);
int wx_reset_hostif(struct wx *wx);
void wx_init_eeprom_params(struct wx *wx);
void wx_get_mac_addr(struct wx *wx, u8 *mac_addr);
void wx_init_rx_addrs(struct wx *wx);
void wx_mac_set_default_filter(struct wx *wx, u8 *addr);
void wx_flush_sw_mac_table(struct wx *wx);
int wx_set_mac(struct net_device *netdev, void *p);
void wx_disable_rx(struct wx *wx);
void wx_set_rx_mode(struct net_device *netdev);
int wx_change_mtu(struct net_device *netdev, int new_mtu);
void wx_disable_rx_queue(struct wx *wx, struct wx_ring *ring);
void wx_configure_rx(struct wx *wx);
void wx_configure(struct wx *wx);
void wx_start_hw(struct wx *wx);
int wx_disable_pcie_master(struct wx *wx);
int wx_stop_adapter(struct wx *wx);
void wx_reset_misc(struct wx *wx);
int wx_get_pcie_msix_counts(struct wx *wx, u16 *msix_count, u16 max_msix_count);
int wx_sw_init(struct wx *wx);
int wx_vlan_rx_add_vid(struct net_device *netdev, __be16 proto, u16 vid);
int wx_vlan_rx_kill_vid(struct net_device *netdev, __be16 proto, u16 vid);

#endif /* _WX_HW_H_ */
