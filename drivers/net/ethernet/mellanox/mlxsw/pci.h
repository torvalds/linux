/*
 * drivers/net/ethernet/mellanox/mlxsw/pci.h
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MLXSW_PCI_H
#define _MLXSW_PCI_H

#include <linux/pci.h>

#define PCI_DEVICE_ID_MELLANOX_SWITCHX2		0xc738
#define PCI_DEVICE_ID_MELLANOX_SPECTRUM		0xcb84
#define PCI_DEVICE_ID_MELLANOX_SWITCHIB		0xcb20
#define PCI_DEVICE_ID_MELLANOX_SWITCHIB2	0xcf08

#if IS_ENABLED(CONFIG_MLXSW_PCI)

int mlxsw_pci_driver_register(struct pci_driver *pci_driver);
void mlxsw_pci_driver_unregister(struct pci_driver *pci_driver);

#else

static inline int
mlxsw_pci_driver_register(struct pci_driver *pci_driver)
{
	return 0;
}

static inline void
mlxsw_pci_driver_unregister(struct pci_driver *pci_driver)
{
}

#endif

#endif
