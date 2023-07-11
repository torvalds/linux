/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx Zynq MPSoC Firmware layer
 *
 *  Copyright (C) 2014-2018 Xilinx
 *
 *  Michal Simek <michal.simek@amd.com>
 *  Davorin Mista <davorin.mista@aggios.com>
 *  Jolly Shah <jollys@xilinx.com>
 *  Rajan Vaja <rajanv@xilinx.com>
 */

#ifndef __FIRMWARE_ZYNQMP_DEBUG_H__
#define __FIRMWARE_ZYNQMP_DEBUG_H__

#if IS_REACHABLE(CONFIG_ZYNQMP_FIRMWARE_DEBUG)
void zynqmp_pm_api_debugfs_init(void);
void zynqmp_pm_api_debugfs_exit(void);
#else
static inline void zynqmp_pm_api_debugfs_init(void) { }
static inline void zynqmp_pm_api_debugfs_exit(void) { }
#endif

#endif /* __FIRMWARE_ZYNQMP_DEBUG_H__ */
