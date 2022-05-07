/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Rockchip PCIe Apis For WIFI
 *
 * Copyright (c) 2022, Rockchip Electronics Co., Ltd.
 */

#ifndef __RK_DHD_PCIE_LINUX_H__
#define __RK_DHD_PCIE_LINUX_H__

#include <typedefs.h>
#include <sbchipc.h>
#include <pcie_core.h>
#include <dhd_pcie.h>
#include <linux/aspm_ext.h>

static inline void
rk_dhd_bus_l1ss_enable_rc_ep(dhd_bus_t *bus, bool enable)
{
	if (!bus->rc_ep_aspm_cap || !bus->rc_ep_l1ss_cap) {
		pr_err("%s: NOT L1SS CAPABLE rc_ep_aspm_cap: %d rc_ep_l1ss_cap: %d\n",
		       __func__, bus->rc_ep_aspm_cap, bus->rc_ep_l1ss_cap);
		return;
	}

	/* Disable ASPM of RC and EP */
	pcie_aspm_ext_l1ss_enable(bus->dev, bus->rc_dev, enable);
}

static inline bool
rk_dhd_bus_is_rc_ep_l1ss_capable(dhd_bus_t *bus)
{
	return pcie_aspm_ext_is_rc_ep_l1ss_capable(bus->dev, bus->rc_dev);
}

#endif /* __RK_DHD_PCIE_LINUX_H__ */
