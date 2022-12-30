/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#ifndef _WX_HW_H_
#define _WX_HW_H_

int wx_check_flash_load(struct wx_hw *hw, u32 check_bit);
void wx_control_hw(struct wx_hw *wxhw, bool drv);
int wx_mng_present(struct wx_hw *wxhw);
int wx_host_interface_command(struct wx_hw *wxhw, u32 *buffer,
			      u32 length, u32 timeout, bool return_data);
int wx_read_ee_hostif(struct wx_hw *wxhw, u16 offset, u16 *data);
int wx_read_ee_hostif_buffer(struct wx_hw *wxhw,
			     u16 offset, u16 words, u16 *data);
int wx_reset_hostif(struct wx_hw *wxhw);
void wx_init_eeprom_params(struct wx_hw *wxhw);
void wx_get_mac_addr(struct wx_hw *wxhw, u8 *mac_addr);
int wx_set_rar(struct wx_hw *wxhw, u32 index, u8 *addr, u64 pools, u32 enable_addr);
int wx_clear_rar(struct wx_hw *wxhw, u32 index);
void wx_init_rx_addrs(struct wx_hw *wxhw);
void wx_disable_rx(struct wx_hw *wxhw);
int wx_disable_pcie_master(struct wx_hw *wxhw);
int wx_stop_adapter(struct wx_hw *wxhw);
void wx_reset_misc(struct wx_hw *wxhw);
int wx_get_pcie_msix_counts(struct wx_hw *wxhw, u16 *msix_count, u16 max_msix_count);
int wx_sw_init(struct wx_hw *wxhw);

#endif /* _WX_HW_H_ */
