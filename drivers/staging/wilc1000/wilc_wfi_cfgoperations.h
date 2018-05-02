/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NM_WFI_CFGOPERATIONS
#define NM_WFI_CFGOPERATIONS
#include "wilc_wfi_netdevice.h"

struct wireless_dev *wilc_create_wiphy(struct net_device *net, struct device *dev);
void wilc_free_wiphy(struct net_device *net);
int wilc_deinit_host_int(struct net_device *net);
int wilc_init_host_int(struct net_device *net);
void WILC_WFI_monitor_rx(u8 *buff, u32 size);
int WILC_WFI_deinit_mon_interface(void);
struct net_device *WILC_WFI_init_mon_interface(const char *name, struct net_device *real_dev);
void wilc_mgmt_frame_register(struct wiphy *wiphy, struct wireless_dev *wdev,
			      u16 frame_type, bool reg);

#endif
