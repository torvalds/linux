/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 Intel Corporation. All rights reserved.
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
 * Tomas Winkler <tomas.winkler@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2008 Intel Corporation. All rights reserved.
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
 *****************************************************************************/


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/init.h>

#include <net/mac80211.h>

#include "iwl-4965-commands.h"
#include "iwl-4965.h"
#include "iwl-core.h"
#include "iwl-debug.h"
#include "iwl-eeprom.h"
#include "iwl-4965-io.h"

/******************************************************************************
 *
 * EEPROM related functions
 *
******************************************************************************/

int iwlcore_eeprom_verify_signature(struct iwl4965_priv *priv)
{
	u32 gp = iwl4965_read32(priv, CSR_EEPROM_GP);
	if ((gp & CSR_EEPROM_GP_VALID_MSK) == CSR_EEPROM_GP_BAD_SIGNATURE) {
		IWL_ERROR("EEPROM not found, EEPROM_GP=0x%08x", gp);
		return -ENOENT;
	}
	return 0;
}
EXPORT_SYMBOL(iwlcore_eeprom_verify_signature);

/*
 * The device's EEPROM semaphore prevents conflicts between driver and uCode
 * when accessing the EEPROM; each access is a series of pulses to/from the
 * EEPROM chip, not a single event, so even reads could conflict if they
 * weren't arbitrated by the semaphore.
 */
int iwlcore_eeprom_acquire_semaphore(struct iwl4965_priv *priv)
{
	u16 count;
	int ret;

	for (count = 0; count < EEPROM_SEM_RETRY_LIMIT; count++) {
		/* Request semaphore */
		iwl4965_set_bit(priv, CSR_HW_IF_CONFIG_REG,
			CSR_HW_IF_CONFIG_REG_BIT_EEPROM_OWN_SEM);

		/* See if we got it */
		ret = iwl4965_poll_bit(priv, CSR_HW_IF_CONFIG_REG,
					CSR_HW_IF_CONFIG_REG_BIT_EEPROM_OWN_SEM,
					CSR_HW_IF_CONFIG_REG_BIT_EEPROM_OWN_SEM,
					EEPROM_SEM_TIMEOUT);
		if (ret >= 0) {
			IWL_DEBUG_IO("Acquired semaphore after %d tries.\n",
				count+1);
			return ret;
		}
	}

	return ret;
}
EXPORT_SYMBOL(iwlcore_eeprom_acquire_semaphore);

void iwlcore_eeprom_release_semaphore(struct iwl4965_priv *priv)
{
	iwl4965_clear_bit(priv, CSR_HW_IF_CONFIG_REG,
		CSR_HW_IF_CONFIG_REG_BIT_EEPROM_OWN_SEM);

}
EXPORT_SYMBOL(iwlcore_eeprom_release_semaphore);


/**
 * iwl_eeprom_init - read EEPROM contents
 *
 * Load the EEPROM contents from adapter into priv->eeprom
 *
 * NOTE:  This routine uses the non-debug IO access functions.
 */
int iwl_eeprom_init(struct iwl4965_priv *priv)
{
	u16 *e = (u16 *)&priv->eeprom;
	u32 gp = iwl4965_read32(priv, CSR_EEPROM_GP);
	u32 r;
	int sz = sizeof(priv->eeprom);
	int ret;
	int i;
	u16 addr;

	/* The EEPROM structure has several padding buffers within it
	 * and when adding new EEPROM maps is subject to programmer errors
	 * which may be very difficult to identify without explicitly
	 * checking the resulting size of the eeprom map. */
	BUILD_BUG_ON(sizeof(priv->eeprom) != IWL_EEPROM_IMAGE_SIZE);

	if ((gp & CSR_EEPROM_GP_VALID_MSK) == CSR_EEPROM_GP_BAD_SIGNATURE) {
		IWL_ERROR("EEPROM not found, EEPROM_GP=0x%08x", gp);
		return -ENOENT;
	}

	/* Make sure driver (instead of uCode) is allowed to read EEPROM */
	ret = priv->cfg->ops->lib->eeprom_ops.acquire_semaphore(priv);
	if (ret < 0) {
		IWL_ERROR("Failed to acquire EEPROM semaphore.\n");
		return -ENOENT;
	}

	/* eeprom is an array of 16bit values */
	for (addr = 0; addr < sz; addr += sizeof(u16)) {
		_iwl4965_write32(priv, CSR_EEPROM_REG, addr << 1);
		_iwl4965_clear_bit(priv, CSR_EEPROM_REG, CSR_EEPROM_REG_BIT_CMD);

		for (i = 0; i < IWL_EEPROM_ACCESS_TIMEOUT;
					i += IWL_EEPROM_ACCESS_DELAY) {
			r = _iwl4965_read_direct32(priv, CSR_EEPROM_REG);
			if (r & CSR_EEPROM_REG_READ_VALID_MSK)
				break;
			udelay(IWL_EEPROM_ACCESS_DELAY);
		}

		if (!(r & CSR_EEPROM_REG_READ_VALID_MSK)) {
			IWL_ERROR("Time out reading EEPROM[%d]", addr);
			ret = -ETIMEDOUT;
			goto done;
		}
		e[addr / 2] = le16_to_cpu((__force __le16)(r >> 16));
	}
	ret = 0;

done:
	priv->cfg->ops->lib->eeprom_ops.release_semaphore(priv);
	return ret;
}
EXPORT_SYMBOL(iwl_eeprom_init);


void iwl_eeprom_get_mac(const struct iwl4965_priv *priv, u8 *mac)
{
	memcpy(mac, priv->eeprom.mac_address, 6);
}
EXPORT_SYMBOL(iwl_eeprom_get_mac);

