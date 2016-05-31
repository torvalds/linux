#include <linux/kernel.h>
#include <linux/mmc/sdio_func.h>

#include "ks_wlan.h"
#include "ks_hostif.h"
#include "ks_wlan_ioctl.h"

static int wep_on_off;
#define	WEP_OFF		0
#define	WEP_ON_64BIT	1
#define	WEP_ON_128BIT	2

static int wep_type;
#define	WEP_KEY_CHARACTER 0
#define	WEP_KEY_HEX	  1

static
void analyze_character_wep_key(struct ks_wlan_parameter *param,
			       int wep_key_index, char *value)
{
	int i;
	unsigned char wep_key[26], key_length;

	key_length = (wep_on_off == WEP_ON_64BIT) ? 5 : 13;
	/* 64bit key_length = 5; 128bit key_length = 13; */

	for (i = 0; i < key_length; i++) {
		wep_key[i] = value[i];
	}

	if (wep_key_index < 0 || wep_key_index > 3)
		return;

	param->wep_key[wep_key_index].size = key_length;
	for (i = 0; i < (param->wep_key[wep_key_index].size); i++) {
		param->wep_key[wep_key_index].val[i] = wep_key[i];
	}
}

static
void analyze_hex_wep_key(struct ks_wlan_parameter *param, int wep_key_index,
			 char *value)
{
	unsigned char wep_end[26], i, j, key_length;

	key_length = (wep_on_off == WEP_ON_64BIT) ? 10 : 26;
	/* 64bit key_length = 10; 128bit key_length = 26; */

	for (i = 0; i < key_length; i++) {
		wep_end[i] = value[i];
		if (i % 2) {
			/* Odd */
			for (j = 0x00; j < 0x10; j++) {
				if (j < 0x0a) {
					if (wep_end[i] == j + 0x30)
						wep_end[i] = j;
				} else {
					if ((wep_end[i] ==
					     j + 0x37) | (wep_end[i] ==
							  j + 0x57))
						wep_end[i] = j;
				}
			}
		} else {
			/* Even */
			for (j = 0x00; j < 0x10; j++) {
				if (j < 0x0a) {
					if (wep_end[i] == j + 0x30) {
						wep_end[i] = j * 16;
					}
				} else {
					if ((wep_end[i] ==
					     j + 0x37) | (wep_end[i] ==
							  j + 0x57))
						wep_end[i] = j * 16;
				}
			}
		}
	}

	for (i = 0; i < key_length / 2; i++) {
		wep_end[i] = wep_end[i * 2] + wep_end[(i * 2) + 1];
	}

	if (wep_key_index < 0 || wep_key_index > 3)
		return;

	param->wep_key[wep_key_index].size = key_length / 2;
	for (i = 0; i < (param->wep_key[wep_key_index].size); i++) {
		param->wep_key[wep_key_index].val[i] = wep_end[i];
	}

}

static
int rate_set_configuration(struct ks_wlan_private *priv, char *value)
{
	int rc = 0;

	priv->reg.tx_rate = TX_RATE_FIXED;
	priv->reg.rate_set.size = 1;

	switch (*value) {
	case '1':	/* 1M 11M 12M 18M */
		if (*(value + 1) == '8') {
			priv->reg.rate_set.body[0] = TX_RATE_18M;
		} else if (*(value + 1) == '2') {
			priv->reg.rate_set.body[0] = TX_RATE_12M | BASIC_RATE;
		} else if (*(value + 1) == '1') {
			priv->reg.rate_set.body[0] = TX_RATE_11M | BASIC_RATE;
		} else {
			priv->reg.rate_set.body[0] = TX_RATE_1M | BASIC_RATE;
		}
		break;
	case '2':	/* 2M 24M */
		if (*(value + 1) == '4') {
			priv->reg.rate_set.body[0] = TX_RATE_24M | BASIC_RATE;
		} else {
			priv->reg.rate_set.body[0] = TX_RATE_2M | BASIC_RATE;
		}
		break;
	case '3':	/* 36M */
		priv->reg.rate_set.body[0] = TX_RATE_36M;
		break;
	case '4':	/* 48M */
		priv->reg.rate_set.body[0] = TX_RATE_48M;
		break;
	case '5':	/* 5.5M 54M */
		if (*(value + 1) == '4') {
			priv->reg.rate_set.body[0] = TX_RATE_54M;
		} else {
			priv->reg.rate_set.body[0] = TX_RATE_5M | BASIC_RATE;
		}
		break;
	case '6':	/* 6M */
		priv->reg.rate_set.body[0] = TX_RATE_6M | BASIC_RATE;
		break;
	case '9':	/* 9M */
		priv->reg.rate_set.body[0] = TX_RATE_9M;
		break;
	case 'K':
		priv->reg.rate_set.body[6] = TX_RATE_36M;
		priv->reg.rate_set.body[5] = TX_RATE_18M;
		priv->reg.rate_set.body[4] = TX_RATE_24M | BASIC_RATE;
		priv->reg.rate_set.body[3] = TX_RATE_12M | BASIC_RATE;
		priv->reg.rate_set.body[2] = TX_RATE_6M | BASIC_RATE;
		priv->reg.rate_set.body[1] = TX_RATE_11M | BASIC_RATE;
		priv->reg.rate_set.body[0] = TX_RATE_2M | BASIC_RATE;
		priv->reg.tx_rate = TX_RATE_FULL_AUTO;
		priv->reg.rate_set.size = 7;
		break;
	default:
		priv->reg.rate_set.body[11] = TX_RATE_54M;
		priv->reg.rate_set.body[10] = TX_RATE_48M;
		priv->reg.rate_set.body[9] = TX_RATE_36M;
		priv->reg.rate_set.body[8] = TX_RATE_18M;
		priv->reg.rate_set.body[7] = TX_RATE_9M;
		priv->reg.rate_set.body[6] = TX_RATE_24M | BASIC_RATE;
		priv->reg.rate_set.body[5] = TX_RATE_12M | BASIC_RATE;
		priv->reg.rate_set.body[4] = TX_RATE_6M | BASIC_RATE;
		priv->reg.rate_set.body[3] = TX_RATE_11M | BASIC_RATE;
		priv->reg.rate_set.body[2] = TX_RATE_5M | BASIC_RATE;
		priv->reg.rate_set.body[1] = TX_RATE_2M | BASIC_RATE;
		priv->reg.rate_set.body[0] = TX_RATE_1M | BASIC_RATE;
		priv->reg.tx_rate = TX_RATE_FULL_AUTO;
		priv->reg.rate_set.size = 12;
		break;
	}
	return rc;
}

#include <linux/firmware.h>
int ks_wlan_read_config_file(struct ks_wlan_private *priv)
{
	struct {
		const int key_len;
		const char *key;
		const char *val;
	} cfg_tbl[] = {
		{15, "BeaconLostCount", "20"}, 			/* 0 */
		{7, "Channel", "1"}, 				/* 1 */
		{17, "FragmentThreshold", "2346"}, 		/* 2 */
		{13, "OperationMode", "Infrastructure"},	/* 3 */
		{19, "PowerManagementMode", "ACTIVE"},	 	/* 4 */
		{12, "RTSThreshold", "2347"}, 			/* 5 */
		{4, "SSID", "default"}, 			/* 6 */
		{6, "TxRate", "Auto"}, 				/* 7 */
		{23, "AuthenticationAlgorithm", ""}, 		/* 8 */
		{12, "WepKeyValue1", ""}, 			/* 9 */
		{12, "WepKeyValue2", ""}, 			/* 10 */
		{12, "WepKeyValue3", ""}, 			/* 11 */
		{12, "WepKeyValue4", ""}, 			/* 12 */
		{8, "WepIndex", "1"}, 				/* 13 */
		{7, "WepType", "STRING"}, 			/* 14 */
		{3, "Wep", "OFF"}, 				/* 15 */
		{13, "PREAMBLE_TYPE", "LONG"}, 			/* 16 */
		{8, "ScanType", "ACTIVE_SCAN"}, 		/* 17 */
		{8, "ROM_FILE", ROM_FILE}, 			/* 18 */
		{7, "PhyType", "BG_MODE"}, 			/* 19 */
		{7, "CtsMode", "FALSE"}, 			/* 20 */
		{19, "PhyInformationTimer", "0"}, 		/* 21 */
		{0, "", ""},
	};

	const struct firmware *fw_entry;
	struct device *dev = NULL;
	char cfg_file[] = CFG_FILE;
	char *cur_p, *end_p;
	char wk_buff[256], *wk_p;

	/* Initialize Variable */
	priv->reg.operation_mode = MODE_INFRASTRUCTURE;	/* Infrastructure */
	priv->reg.channel = 10;	/* 10 */
	memset(priv->reg.bssid, 0x0, ETH_ALEN);	/* BSSID */
	priv->reg.ssid.body[0] = '\0';	/* SSID */
	priv->reg.ssid.size = 0;	/* SSID size */
	priv->reg.tx_rate = TX_RATE_AUTO;	/* TxRate Fully Auto */
	priv->reg.preamble = LONG_PREAMBLE;	/* Preamble = LONG */
	priv->reg.powermgt = POWMGT_ACTIVE_MODE;	/* POWMGT_ACTIVE_MODE */
	priv->reg.scan_type = ACTIVE_SCAN;	/* Active */
	priv->reg.beacon_lost_count = 20;	/* Beacon Lost Count */
	priv->reg.rts = 2347UL;	/* RTS Threashold */
	priv->reg.fragment = 2346UL;	/* Fragmentation Threashold */

	strcpy(&priv->reg.rom_file[0], ROM_FILE);

	priv->skb = NULL;

	priv->reg.authenticate_type = AUTH_TYPE_OPEN_SYSTEM;	/* AuthenticationAlgorithm */

	priv->reg.privacy_invoked = 0x00;	/* WEP */
	priv->reg.wep_index = 0;
	memset(&priv->reg.wep_key[0], 0, sizeof(priv->reg.wep_key[0]));
	memset(&priv->reg.wep_key[1], 0, sizeof(priv->reg.wep_key[0]));
	memset(&priv->reg.wep_key[2], 0, sizeof(priv->reg.wep_key[0]));
	memset(&priv->reg.wep_key[3], 0, sizeof(priv->reg.wep_key[0]));

	priv->reg.phy_type = D_11BG_COMPATIBLE_MODE;
	priv->reg.cts_mode = CTS_MODE_FALSE;
	priv->reg.phy_info_timer = 0;
	priv->reg.rate_set.body[11] = TX_RATE_54M;
	priv->reg.rate_set.body[10] = TX_RATE_48M;
	priv->reg.rate_set.body[9] = TX_RATE_36M;
	priv->reg.rate_set.body[8] = TX_RATE_18M;
	priv->reg.rate_set.body[7] = TX_RATE_9M;
	priv->reg.rate_set.body[6] = TX_RATE_24M | BASIC_RATE;
	priv->reg.rate_set.body[5] = TX_RATE_12M | BASIC_RATE;
	priv->reg.rate_set.body[4] = TX_RATE_6M | BASIC_RATE;
	priv->reg.rate_set.body[3] = TX_RATE_11M | BASIC_RATE;
	priv->reg.rate_set.body[2] = TX_RATE_5M | BASIC_RATE;
	priv->reg.rate_set.body[1] = TX_RATE_2M | BASIC_RATE;
	priv->reg.rate_set.body[0] = TX_RATE_1M | BASIC_RATE;
	priv->reg.tx_rate = TX_RATE_FULL_AUTO;
	priv->reg.rate_set.size = 12;

	dev = &priv->ks_wlan_hw.sdio_card->func->dev;
	/* If no cfg file, stay with the defaults */
	if (request_firmware_direct(&fw_entry, cfg_file, dev))
		return 0;

	DPRINTK(4, "success request_firmware() file=%s size=%zu\n", cfg_file,
		fw_entry->size);
	cur_p = fw_entry->data;
	end_p = cur_p + fw_entry->size;
	*end_p = '\0';

	while (cur_p < end_p) {
		int i, j, len;

		len = end_p - cur_p;
		for (i = 0; cfg_tbl[i].key_len != 0; i++) {
			if (*cur_p == '#') {
				break;
			}
			if (len < cfg_tbl[i].key_len) {
				continue;
			}
			if (!strncmp(cfg_tbl[i].key, cur_p, cfg_tbl[i].key_len)) {
				break;
			}
		}
		if ((*cur_p == '#') || (cfg_tbl[i].key_len == 0)) {
			while (*cur_p != '\n') {
				if (cur_p >= end_p) {
					break;
				}
				cur_p++;
			}
			cur_p++;
		} else {
			cur_p += cfg_tbl[i].key_len;
			if (*cur_p != '=') {
				while (*cur_p != '\n') {
					if (cur_p >= end_p) {
						break;
					}
					cur_p++;
				}
				continue;
			}
			cur_p++;

			for (j = 0, wk_p = cur_p; *wk_p != '\n' && wk_p < end_p;
			     j++, wk_p++) {
				wk_buff[j] = *wk_p;
			}
			wk_buff[j] = '\0';
			cur_p = wk_p;
			DPRINTK(4, "%s=%s\n", cfg_tbl[i].key, wk_buff);
			wk_p = wk_buff;

			switch (i) {
			case 0:	/* "BeaconLostCount", "10" */
				priv->reg.beacon_lost_count =
				    simple_strtol(wk_buff, NULL, 10);
				break;
			case 1:	/* "Channel", "1" */
				priv->reg.channel =
				    simple_strtol(wk_buff, NULL, 10);
				break;
			case 2:	/* "FragmentThreshold","2346" */
				j = simple_strtol(wk_buff, NULL, 10);
				priv->reg.fragment = (unsigned long)j;
				break;
			case 3:	/* "OperationMode","Infrastructure" */
				switch (*wk_buff) {
				case 'P':
					priv->reg.operation_mode =
					    MODE_PSEUDO_ADHOC;
					break;
				case 'I':
					priv->reg.operation_mode =
					    MODE_INFRASTRUCTURE;
					break;
				case '8':
					priv->reg.operation_mode = MODE_ADHOC;
					break;
				default:
					priv->reg.operation_mode =
					    MODE_INFRASTRUCTURE;
				}
				break;
			case 4:	/* "PowerManagementMode","POWER_ACTIVE" */
				if (!strncmp(wk_buff, "SAVE1", 5)) {
					priv->reg.powermgt = POWMGT_SAVE1_MODE;
				} else if (!strncmp(wk_buff, "SAVE2", 5)) {
					priv->reg.powermgt = POWMGT_SAVE2_MODE;
				} else {
					priv->reg.powermgt = POWMGT_ACTIVE_MODE;
				}
				break;
			case 5:	/* "RTSThreshold","2347" */
				j = simple_strtol(wk_buff, NULL, 10);
				priv->reg.rts = (unsigned long)j;
				break;
			case 6:	/* "SSID","" */
				if (*wk_p != '"')
					break;
				wk_p++;
				for (j = 0; *wk_p != '"'; j++) {
					if (wk_p == '\0') {
						break;
					}
					priv->reg.ssid.body[j] = *wk_p++;
				}
				priv->reg.ssid.body[j] = '\0';
				priv->reg.ssid.size = j;
				wk_p++;
				break;
			case 7:	/* "TxRate","Auto" */
				rate_set_configuration(priv, wk_p);
				break;
			case 8:	/* "AuthenticationAlgorithm","OPEN_SYSTEM" */
				switch (*wk_p) {
				case 'O':	/* Authenticate System : Open System */
					priv->reg.authenticate_type =
					    AUTH_TYPE_OPEN_SYSTEM;
					break;
				case 'S':	/* Authenticate System : Shared Key */
					priv->reg.authenticate_type =
					    AUTH_TYPE_SHARED_KEY;
					break;
				}
				break;
			case 9:	/* "WepKeyValue1","" */
			case 10:	/* "WepKeyValue2","" */
			case 11:	/* "WepKeyValue3","" */
			case 12:	/* "WepKeyValue4","" */
				if (wep_on_off != WEP_OFF) {
					switch (wep_type) {
					case WEP_KEY_CHARACTER:
						analyze_character_wep_key
						    (&priv->reg, (i - 9), wk_p);
						break;
					case WEP_KEY_HEX:
						analyze_hex_wep_key(&priv->reg,
								    (i - 9),
								    wk_p);
						break;
					}
				}
				break;
			case 13:	/* "WepIndex","1"->0 (So, Zero Origin) */
				priv->reg.wep_index =
				    simple_strtol(wk_buff, NULL, 10) - 1;
				break;
			case 14:	/* "WepType","STRING" */
				if (!strncmp(wk_buff, "STRING", 6)) {
					wep_type = WEP_KEY_CHARACTER;
				} else {
					wep_type = WEP_KEY_HEX;
				}
				break;
			case 15:	/* "Wep","OFF" */
				if (!strncmp(wk_buff, "OFF", 3)) {
					priv->reg.privacy_invoked = 0x00;
					wep_on_off = WEP_OFF;
				} else {	/* 64bit or 128bit */
					priv->reg.privacy_invoked = 0x01;
					if (*wk_buff == '6') {	/* 64bit */
						wep_on_off = WEP_ON_64BIT;
					} else {	/* 128bit */
						wep_on_off = WEP_ON_128BIT;
					}
				}
				break;
			case 16:	/* "PREAMBLE_TYPE","LONG" */
				if (!strncmp(wk_buff, "SHORT", 5)) {
					priv->reg.preamble = SHORT_PREAMBLE;
				} else {	/* "LONG" */
					priv->reg.preamble = LONG_PREAMBLE;
				}
				break;
			case 17:	/* "ScanType","ACTIVE_SCAN" */
				if (!strncmp(wk_buff, "PASSIVE_SCAN", 12)) {
					priv->reg.scan_type = PASSIVE_SCAN;
				} else {	/* "ACTIVE_SCAN" */
					priv->reg.scan_type = ACTIVE_SCAN;
				}
				break;
			case 18:	// "ROM_FILE",ROMFILE
				if (*wk_p != '"')
					break;
				wk_p++;
				for (j = 0; *wk_p != '"'; j++) {
					if (wk_p == '\0') {
						break;
					}
					priv->reg.rom_file[j] = *wk_p++;
				}
				priv->reg.rom_file[j] = '\0';
				wk_p++;
				break;
			case 19:	/*"PhyType", "BG_MODE" */
				if (!strncmp(wk_buff, "B_MODE", 6)) {
					priv->reg.phy_type = D_11B_ONLY_MODE;
				} else if (!strncmp(wk_buff, "G_MODE", 6)) {
					priv->reg.phy_type = D_11G_ONLY_MODE;
				} else {
					priv->reg.phy_type =
					    D_11BG_COMPATIBLE_MODE;
				}
				break;
			case 20:	/* "CtsMode", "FALSE" */
				if (!strncmp(wk_buff, "TRUE", 4)) {
					priv->reg.cts_mode = CTS_MODE_TRUE;
				} else {
					priv->reg.cts_mode = CTS_MODE_FALSE;
				}
				break;
			case 21:	/* "PhyInformationTimer", "0" */
				j = simple_strtol(wk_buff, NULL, 10);
				priv->reg.phy_info_timer = (uint16_t) j;
				break;
			default:
				break;
			}
			if (cur_p >= end_p) {
				break;
			}
			cur_p++;
		}

	}
	release_firmware(fw_entry);

	DPRINTK(3,
		"\n    operation_mode = %d\n    channel = %d\n    ssid = %s\n    tx_rate = %d\n \
   preamble = %d\n    powermgt = %d\n    scan_type = %d\n    beacon_lost_count = %d\n    rts = %d\n \
   fragment = %d\n    privacy_invoked = %d\n    wep_type = %d\n    wep_on_off = %d\n    wep_index = %d\n    romfile = %s\n",
		priv->reg.operation_mode, priv->reg.channel, &priv->reg.ssid.body[0], priv->reg.tx_rate, priv->reg.preamble, priv->reg.powermgt, priv->reg.scan_type, priv->reg.beacon_lost_count, priv->reg.rts, priv->reg.fragment, priv->reg.privacy_invoked, wep_type, wep_on_off,
		priv->reg.wep_index, &priv->reg.rom_file[0]
	    );
	DPRINTK(3,
		"\n    phy_type = %d\n    cts_mode = %d\n    tx_rate = %d\n    phy_info_timer = %d\n",
		priv->reg.phy_type, priv->reg.cts_mode, priv->reg.tx_rate,
		priv->reg.phy_info_timer);

	return (0);
}
