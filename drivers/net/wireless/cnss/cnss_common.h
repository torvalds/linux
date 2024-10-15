/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _NET_CNSS_COMMON_H_
#define _NET_CNSS_COMMON_H_

/* max 20mhz channel count */
#define CNSS_MAX_CH_NUM		45

struct cnss_cap_tsf_info {
	int irq_num;
	void *context;
	irq_handler_t irq_handler;
};

struct cnss_dev_platform_ops {
	int (*request_bus_bandwidth)(int bandwidth);
	void* (*get_virt_ramdump_mem)(unsigned long *size);
	void (*device_self_recovery)(void);
	void (*schedule_recovery_work)(void);
	void (*device_crashed)(void);
	u8 * (*get_wlan_mac_address)(u32 *num);
	int (*set_wlan_mac_address)(const u8 *in, u32 len);
	int (*power_up)(struct device *dev);
	int (*power_down)(struct device *dev);
	int (*register_tsf_captured_handler)(irq_handler_t handler,
					     void *adapter);
	int (*unregister_tsf_captured_handler)(void *adapter);
};

int cnss_pci_request_bus_bandwidth(int bandwidth);
int cnss_sdio_request_bus_bandwidth(int bandwidth);

void cnss_sdio_device_crashed(void);
void cnss_pci_device_crashed(void);

void cnss_pci_device_self_recovery(void);
void cnss_sdio_device_self_recovery(void);

void *cnss_pci_get_virt_ramdump_mem(unsigned long *size);
void *cnss_sdio_get_virt_ramdump_mem(unsigned long *size);

void cnss_sdio_schedule_recovery_work(void);
void cnss_pci_schedule_recovery_work(void);

int cnss_pcie_set_wlan_mac_address(const u8 *in, u32 len);
int cnss_sdio_set_wlan_mac_address(const u8 *in, u32 len);

u8 *cnss_pci_get_wlan_mac_address(u32 *num);
u8 *cnss_sdio_get_wlan_mac_address(u32 *num);
int cnss_sdio_power_up(struct device *dev);
int cnss_sdio_power_down(struct device *dev);
int cnss_pcie_power_up(struct device *dev);
int cnss_pcie_power_down(struct device *dev);
const char *cnss_wlan_get_evicted_data_file(void);
#endif /* _NET_CNSS_COMMON_H_ */
