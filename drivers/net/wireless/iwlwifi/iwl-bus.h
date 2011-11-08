/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#ifndef __iwl_bus_h__
#define __iwl_bus_h__

#include <linux/types.h>
#include <linux/spinlock.h>

/**
 * DOC: Bus layer - role and goal
 *
 * iwl-bus.h defines the API to the bus layer of the iwlwifi driver.
 * The bus layer is responsible for doing very basic bus operations that are
 * listed in the iwl_bus_ops structure.
 * The bus layer registers to the bus driver, advertises the supported HW and
 * gets notifications about enumeration, suspend, resume.
 * For the moment, the bus layer is not a linux kernel module as itself, and
 * the module_init function of the driver must call the bus specific
 * registration functions. These functions are listed at the end of this file.
 * For the moment, there is only one implementation of this interface: PCI-e.
 * This implementation is iwl-pci.c
 */

/**
 * DOC: encapsulation and type safety
 *
 * The iwl_bus describes the data that is shared amongst all the bus layer
 * implementations. This data is visible to other layers. Data in the bus
 * specific area is not visible outside the bus specific implementation.
 * iwl_bus holds a pointer to iwl_shared which holds pointer to all the other
 * layers of the driver (iwl_priv, iwl_trans). In fact, this is the way to go
 * when the transport layer needs to call a function of another layer.
 *
 * In order to achieve encapsulation, iwl_priv cannot be dereferenced from the
 * bus layer. Type safety is still kept since functions that gets iwl_priv gets
 * a typed pointer (as opposed to void *).
 */

/**
 * DOC: probe flow
 *
 * The module_init calls the bus specific registration function. The
 * registration to the bus layer will trigger an enumeration of the bus which
 * will call the bus specific probe function.
 * The first thing this function must do is to allocate the memory needed by
 * iwl_bus + the bus_specific data.
 * Once the bus specific probe function has configured the hardware, it
 * chooses the appropriate transport layer and calls iwl_probe that will run
 * the bus independent probe flow.
 *
 * Note: The bus specific code must set the following data in iwl_bus before it
 *       calls iwl_probe:
 *	* bus->dev
 *	* bus->irq
 *	* bus->ops
 */

struct iwl_shared;
struct iwl_bus;

/**
 * struct iwl_bus_ops - bus specific operations
 * @get_pm_support: must returns true if the bus can go to sleep
 * @apm_config: will be called during the config of the APM
 * @get_hw_id: prints the hw_id in the provided buffer
 * @write8: write a byte to register at offset ofs
 * @write32: write a dword to register at offset ofs
 * @wread32: read a dword at register at offset ofs
 */
struct iwl_bus_ops {
	bool (*get_pm_support)(struct iwl_bus *bus);
	void (*apm_config)(struct iwl_bus *bus);
	void (*get_hw_id)(struct iwl_bus *bus, char buf[], int buf_len);
	void (*write8)(struct iwl_bus *bus, u32 ofs, u8 val);
	void (*write32)(struct iwl_bus *bus, u32 ofs, u32 val);
	u32 (*read32)(struct iwl_bus *bus, u32 ofs);
};

/**
 * struct iwl_bus - bus common data
 *
 * This data is common to all bus layer implementations.
 *
 * @dev - pointer to struct device * that represents the device
 * @ops - pointer to iwl_bus_ops
 * @shrd - pointer to iwl_shared which holds shared data from the upper layer
 *	NB: for the time being this needs to be set by the upper layer since
 *	it allocates the shared data
 * @irq - the irq number for the device
 * @reg_lock - protect hw register access
 */
struct iwl_bus {
	struct device *dev;
	const struct iwl_bus_ops *ops;
	struct iwl_shared *shrd;

	unsigned int irq;
	spinlock_t reg_lock;

	/* pointer to bus specific struct */
	/*Ensure that this pointer will always be aligned to sizeof pointer */
	char bus_specific[0] __attribute__((__aligned__(sizeof(void *))));
};

static inline bool bus_get_pm_support(struct iwl_bus *bus)
{
	return bus->ops->get_pm_support(bus);
}

static inline void bus_apm_config(struct iwl_bus *bus)
{
	bus->ops->apm_config(bus);
}

static inline void bus_get_hw_id(struct iwl_bus *bus, char buf[], int buf_len)
{
	bus->ops->get_hw_id(bus, buf, buf_len);
}

static inline void bus_write8(struct iwl_bus *bus, u32 ofs, u8 val)
{
	bus->ops->write8(bus, ofs, val);
}

static inline void bus_write32(struct iwl_bus *bus, u32 ofs, u32 val)
{
	bus->ops->write32(bus, ofs, val);
}

static inline u32 bus_read32(struct iwl_bus *bus, u32 ofs)
{
	return bus->ops->read32(bus, ofs);
}

/*****************************************************
* Bus layer registration functions
******************************************************/
int __must_check iwl_pci_register_driver(void);
void iwl_pci_unregister_driver(void);

#endif /* __iwl_bus_h__ */
