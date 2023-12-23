/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */
#ifndef _OCTEP_VF_MBOX_H_
#define _OCTEP_VF_MBOX_H_

#define OCTEP_PFVF_MBOX_MAX_DATA_BUF_SIZE 256

int octep_vf_setup_mbox(struct octep_vf_device *oct);
void octep_vf_delete_mbox(struct octep_vf_device *oct);
int octep_vf_mbox_set_mtu(struct octep_vf_device *oct, int mtu);
int octep_vf_mbox_set_mac_addr(struct octep_vf_device *oct, char *mac_addr);
int octep_vf_mbox_get_mac_addr(struct octep_vf_device *oct, char *mac_addr);
int octep_vf_mbox_version_check(struct octep_vf_device *oct);
int octep_vf_mbox_set_rx_state(struct octep_vf_device *oct, bool state);
int octep_vf_mbox_set_link_status(struct octep_vf_device *oct, bool status);
int octep_vf_mbox_get_link_status(struct octep_vf_device *oct, u8 *oper_up);
int octep_vf_mbox_dev_remove(struct octep_vf_device *oct);
int octep_vf_mbox_get_fw_info(struct octep_vf_device *oct);
int octep_vf_mbox_set_offloads(struct octep_vf_device *oct, u16 tx_offloads, u16 rx_offloads);

#endif
