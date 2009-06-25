/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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
 *
 * Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
 *
 */

#include <linux/kernel.h>

#include "iwm.h"
#include "umac.h"
#include "commands.h"
#include "eeprom.h"

static struct iwm_eeprom_entry eeprom_map[] = {
	[IWM_EEPROM_SIG] =
	{"Signature", IWM_EEPROM_SIG_OFF, IWM_EEPROM_SIG_LEN},

	[IWM_EEPROM_VERSION] =
	{"Version", IWM_EEPROM_VERSION_OFF, IWM_EEPROM_VERSION_LEN},

	[IWM_EEPROM_OEM_HW_VERSION] =
	{"OEM HW version", IWM_EEPROM_OEM_HW_VERSION_OFF,
	 IWM_EEPROM_OEM_HW_VERSION_LEN},

	[IWM_EEPROM_MAC_VERSION] =
	{"MAC version", IWM_EEPROM_MAC_VERSION_OFF, IWM_EEPROM_MAC_VERSION_LEN},

	[IWM_EEPROM_CARD_ID] =
	{"Card ID", IWM_EEPROM_CARD_ID_OFF, IWM_EEPROM_CARD_ID_LEN},

	[IWM_EEPROM_RADIO_CONF] =
	{"Radio config", IWM_EEPROM_RADIO_CONF_OFF, IWM_EEPROM_RADIO_CONF_LEN},

	[IWM_EEPROM_SKU_CAP] =
	{"SKU capabilities", IWM_EEPROM_SKU_CAP_OFF, IWM_EEPROM_SKU_CAP_LEN},

	[IWM_EEPROM_CALIB_RXIQ_OFFSET] =
	{"RX IQ offset", IWM_EEPROM_CALIB_RXIQ_OFF, IWM_EEPROM_INDIRECT_LEN},

	[IWM_EEPROM_CALIB_RXIQ] =
	{"Calib RX IQ", 0, IWM_EEPROM_CALIB_RXIQ_LEN},
};


static int iwm_eeprom_read(struct iwm_priv *iwm, u8 eeprom_id)
{
	int ret;
	u32 entry_size, chunk_size, data_offset = 0, addr_offset = 0;
	u32 addr;
	struct iwm_udma_wifi_cmd udma_cmd;
	struct iwm_umac_cmd umac_cmd;
	struct iwm_umac_cmd_eeprom_proxy eeprom_cmd;

	if (eeprom_id > (IWM_EEPROM_LAST - 1))
		return -EINVAL;

	entry_size = eeprom_map[eeprom_id].length;

	if (eeprom_id >= IWM_EEPROM_INDIRECT_DATA) {
		/* indirect data */
		u32 off_id = eeprom_id - IWM_EEPROM_INDIRECT_DATA +
			     IWM_EEPROM_INDIRECT_OFFSET;

		eeprom_map[eeprom_id].offset =
			*(u16 *)(iwm->eeprom + eeprom_map[off_id].offset) << 1;
	}

	addr = eeprom_map[eeprom_id].offset;

	udma_cmd.eop = 1;
	udma_cmd.credit_group = 0x4;
	udma_cmd.ra_tid = UMAC_HDI_ACT_TBL_IDX_HOST_CMD;
	udma_cmd.lmac_offset = 0;

	umac_cmd.id = UMAC_CMD_OPCODE_EEPROM_PROXY;
	umac_cmd.resp = 1;

	while (entry_size > 0) {
		chunk_size = min_t(u32, entry_size, IWM_MAX_EEPROM_DATA_LEN);

		eeprom_cmd.hdr.type =
			cpu_to_le32(IWM_UMAC_CMD_EEPROM_TYPE_READ);
		eeprom_cmd.hdr.offset = cpu_to_le32(addr + addr_offset);
		eeprom_cmd.hdr.len = cpu_to_le32(chunk_size);

		ret = iwm_hal_send_umac_cmd(iwm, &udma_cmd,
					    &umac_cmd, &eeprom_cmd,
				     sizeof(struct iwm_umac_cmd_eeprom_proxy));
		if (ret < 0) {
			IWM_ERR(iwm, "Couldn't read eeprom\n");
			return ret;
		}

		ret = iwm_notif_handle(iwm, UMAC_CMD_OPCODE_EEPROM_PROXY,
				       IWM_SRC_UMAC, 2*HZ);
		if (ret < 0) {
			IWM_ERR(iwm, "Did not get any eeprom answer\n");
			return ret;
		}

		data_offset += chunk_size;
		addr_offset += chunk_size;
		entry_size -= chunk_size;
	}

	return 0;
}

u8 *iwm_eeprom_access(struct iwm_priv *iwm, u8 eeprom_id)
{
	if (!iwm->eeprom)
		return ERR_PTR(-ENODEV);

	return iwm->eeprom + eeprom_map[eeprom_id].offset;
}

int iwm_eeprom_init(struct iwm_priv *iwm)
{
	int i, ret = 0;
	char name[32];

	iwm->eeprom = kzalloc(IWM_EEPROM_LEN, GFP_KERNEL);
	if (!iwm->eeprom)
		return -ENOMEM;

	for (i = IWM_EEPROM_FIRST; i < IWM_EEPROM_LAST; i++) {
		ret = iwm_eeprom_read(iwm, i);
		if (ret < 0) {
			IWM_ERR(iwm, "Couldn't read eeprom entry #%d: %s\n",
				i, eeprom_map[i].name);
			break;
		}
	}

	IWM_DBG_BOOT(iwm, DBG, "EEPROM dump:\n");
	for (i = IWM_EEPROM_FIRST; i < IWM_EEPROM_LAST; i++) {
		memset(name, 0, 32);
		sprintf(name, "%s: ", eeprom_map[i].name);

		IWM_HEXDUMP(iwm, DBG, BOOT, name,
			    iwm->eeprom + eeprom_map[i].offset,
			    eeprom_map[i].length);
	}

	return ret;
}

void iwm_eeprom_exit(struct iwm_priv *iwm)
{
	kfree(iwm->eeprom);
}
