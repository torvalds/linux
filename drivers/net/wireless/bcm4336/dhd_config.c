
#include <typedefs.h>
#include <osl.h>

#include <bcmutils.h>
#include <hndsoc.h>
#if defined(HW_OOB)
#include <bcmdefs.h>
#include <bcmsdh.h>
#include <sdio.h>
#include <sbchipc.h>
#endif

#include <dhd_config.h>
#include <dhd_dbg.h>

/* message levels */
#define CONFIG_ERROR_LEVEL	0x0001
#define CONFIG_TRACE_LEVEL	0x0002

uint config_msg_level = CONFIG_ERROR_LEVEL;

#define CONFIG_ERROR(x) \
	do { \
		if (config_msg_level & CONFIG_ERROR_LEVEL) { \
			printk(KERN_ERR "CONFIG-ERROR) ");	\
			printk x; \
		} \
	} while (0)
#define CONFIG_TRACE(x) \
	do { \
		if (config_msg_level & CONFIG_TRACE_LEVEL) { \
			printk(KERN_ERR "CONFIG-TRACE) ");	\
			printk x; \
		} \
	} while (0)

#define SBSDIO_CIS_SIZE_LIMIT		0x200		/* maximum bytes in one CIS */
#define MAXSZ_BUF		1000
#define	MAXSZ_CONFIG	4096

#define BCM43362A0_CHIP_REV     0
#define BCM43362A2_CHIP_REV     1
#define BCM43430A0_CHIP_REV     0
#define BCM4330B2_CHIP_REV      4
#define BCM43340B0_CHIP_REV     2
#define BCM43341B0_CHIP_REV     2
#define BCM43241B4_CHIP_REV     5
#define BCM4335A0_CHIP_REV      2
#define BCM4339A0_CHIP_REV      1
#define BCM4354A1_CHIP_REV      1
#define BCM4356A2_CHIP_REV      2

#define FW_TYPE_STA     0
#define FW_TYPE_APSTA   1
#define FW_TYPE_P2P     2
#define FW_TYPE_MFG     3
#define FW_TYPE_G       0
#define FW_TYPE_AG      1

const static char *bcm4330b2_fw_name[] = {
	"fw_bcm40183b2.bin",
	"fw_bcm40183b2_apsta.bin",
	"fw_bcm40183b2_p2p.bin",
	"fw_bcm40183b2_mfg.bin"
};

const static char *bcm4330b2_ag_fw_name[] = {
	"fw_bcm40183b2_ag.bin",
	"fw_bcm40183b2_ag_apsta.bin",
	"fw_bcm40183b2_ag_p2p.bin",
	"fw_bcm40183b2_ag_mfg.bin"
};

const static char *bcm43362a0_fw_name[] = {
	"fw_bcm40181a0.bin",
	"fw_bcm40181a0_apsta.bin",
	"fw_bcm40181a0_p2p.bin",
	"fw_bcm40181a0_mfg.bin"
};

const static char *bcm43362a2_fw_name[] = {
	"fw_bcm40181a2.bin",
	"fw_bcm40181a2_apsta.bin",
	"fw_bcm40181a2_p2p.bin",
	"fw_bcm40181a2_mfg.bin"
};

const static char *bcm43438a0_fw_name[] = {
	"fw_bcm43438a0.bin",
	"fw_bcm43438a0_apsta.bin",
	"fw_bcm43438a0_p2p.bin",
	"fw_bcm43438a0_mfg.bin"
};

const static char *bcm43341b0_ag_fw_name[] = {
	"fw_bcm43341b0_ag.bin",
	"fw_bcm43341b0_ag_apsta.bin",
	"fw_bcm43341b0_ag_p2p.bin",
	"fw_bcm43341b0_ag_mfg.bin"
};

const static char *bcm43241b4_ag_fw_name[] = {
	"fw_bcm43241b4_ag.bin",
	"fw_bcm43241b4_ag_apsta.bin",
	"fw_bcm43241b4_ag_p2p.bin",
	"fw_bcm43241b4_ag_mfg.bin"
};

const static char *bcm4339a0_ag_fw_name[] = {
	"fw_bcm4339a0_ag.bin",
	"fw_bcm4339a0_ag_apsta.bin",
	"fw_bcm4339a0_ag_p2p.bin",
	"fw_bcm4339a0_ag_mfg.bin"
};

const static char *bcm4354a1_ag_fw_name[] = {
	"fw_bcm4354a1_ag.bin",
	"fw_bcm4354a1_ag_apsta.bin",
	"fw_bcm4354a1_ag_p2p.bin",
	"fw_bcm4354a1_ag_mfg.bin"
};

const static char *bcm4356a2_ag_fw_name[] = {
	"fw_bcm4356a2_ag.bin",
	"fw_bcm4356a2_ag_apsta.bin",
	"fw_bcm4356a2_ag_p2p.bin",
	"fw_bcm4356a2_ag_mfg.bin"
};

#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i
#define htodchanspec(i) i
#define dtohchanspec(i) i

void
dhd_conf_free_mac_list(wl_mac_list_ctrl_t *mac_list)
{
	CONFIG_TRACE(("%s called\n", __FUNCTION__));

	if (mac_list->m_mac_list_head) {
		CONFIG_TRACE(("%s Free %p\n", __FUNCTION__, mac_list->m_mac_list_head));
		if (mac_list->m_mac_list_head->mac) {
			CONFIG_TRACE(("%s Free %p\n", __FUNCTION__, mac_list->m_mac_list_head->mac));
			kfree(mac_list->m_mac_list_head->mac);
		}
		kfree(mac_list->m_mac_list_head);
	}
	mac_list->count = 0;
}

int
dhd_conf_get_mac(dhd_pub_t *dhd, bcmsdh_info_t *sdh, uint8 *mac)
{
	int i, err = -1;
	uint8 *ptr = 0;
	unsigned char tpl_code, tpl_link='\0';
	uint8 header[3] = {0x80, 0x07, 0x19};
	uint8 *cis;

	if (!(cis = MALLOC(dhd->osh, SBSDIO_CIS_SIZE_LIMIT))) {
		CONFIG_ERROR(("%s: cis malloc failed\n", __FUNCTION__));
		return err;
	}
	bzero(cis, SBSDIO_CIS_SIZE_LIMIT);

	if ((err = bcmsdh_cis_read(sdh, 0, cis, SBSDIO_CIS_SIZE_LIMIT))) {
		CONFIG_ERROR(("%s: cis read err %d\n", __FUNCTION__, err));
		MFREE(dhd->osh, cis, SBSDIO_CIS_SIZE_LIMIT);
		return err;
	}
	err = -1; // reset err;
	ptr = cis;
	do {
		/* 0xff means we're done */
		tpl_code = *ptr;
		ptr++;
		if (tpl_code == 0xff)
			break;

		/* null entries have no link field or data */
		if (tpl_code == 0x00)
			continue;

		tpl_link = *ptr;
		ptr++;
		/* a size of 0xff also means we're done */
		if (tpl_link == 0xff)
			break;
		if (config_msg_level & CONFIG_TRACE_LEVEL) {
			printf("%s: tpl_code=0x%02x, tpl_link=0x%02x, tag=0x%02x\n",
				__FUNCTION__, tpl_code, tpl_link, *ptr);
			printf("%s: value:", __FUNCTION__);
			for (i=0; i<tpl_link-1; i++) {
				printf("%02x ", ptr[i+1]);
				if ((i+1)%16==0)
					printf("\n");
			}
			printf("\n");
		}

		if (tpl_code == 0x80 && tpl_link == 0x07 && *ptr == 0x19)
			break;

		ptr += tpl_link;
	} while (1);

	if (tpl_code == 0x80 && tpl_link == 0x07 && *ptr == 0x19) {
		/* Normal OTP */
		memcpy(mac, ptr+1, 6);
		err = 0;
	} else {
		ptr = cis;
		/* Special OTP */
		if (bcmsdh_reg_read(sdh, SI_ENUM_BASE, 4) == 0x16044330) {
			for (i=0; i<SBSDIO_CIS_SIZE_LIMIT; i++) {
				if (!memcmp(header, ptr, 3)) {
					memcpy(mac, ptr+1, 6);
					err = 0;
					break;
				}
				ptr++;
			}
		}
	}

	ASSERT(cis);
	MFREE(dhd->osh, cis, SBSDIO_CIS_SIZE_LIMIT);

	return err;
}

void
dhd_conf_set_fw_name_by_mac(dhd_pub_t *dhd, bcmsdh_info_t *sdh, char *fw_path)
{
	int i, j;
	uint8 mac[6]={0};
	int fw_num=0, mac_num=0;
	uint32 oui, nic;
	wl_mac_list_t *mac_list;
	wl_mac_range_t *mac_range;
	char *pfw_name;
	int fw_type, fw_type_new;

	mac_list = dhd->conf->fw_by_mac.m_mac_list_head;
	fw_num = dhd->conf->fw_by_mac.count;
	if (!mac_list || !fw_num)
		return;

	if (dhd_conf_get_mac(dhd, sdh, mac)) {
		CONFIG_ERROR(("%s: Can not read MAC address\n", __FUNCTION__));
		return;
	}
	oui = (mac[0] << 16) | (mac[1] << 8) | (mac[2]);
	nic = (mac[3] << 16) | (mac[4] << 8) | (mac[5]);

	/* find out the last '/' */
	i = strlen(fw_path);
	while (i>0){
		if (fw_path[i] == '/') break;
		i--;
	}
	pfw_name = &fw_path[i+1];
	fw_type = (strstr(pfw_name, "_mfg") ?
		FW_TYPE_MFG : (strstr(pfw_name, "_apsta") ?
		FW_TYPE_APSTA : (strstr(pfw_name, "_p2p") ?
		FW_TYPE_P2P : FW_TYPE_STA)));

	for (i=0; i<fw_num; i++) {
		mac_num = mac_list[i].count;
		mac_range = mac_list[i].mac;
		fw_type_new = (strstr(mac_list[i].name, "_mfg") ?
			FW_TYPE_MFG : (strstr(mac_list[i].name, "_apsta") ?
			FW_TYPE_APSTA : (strstr(mac_list[i].name, "_p2p") ?
			FW_TYPE_P2P : FW_TYPE_STA)));
		if (fw_type != fw_type_new) {
			printf("%s: fw_typ=%d != fw_type_new=%d\n", __FUNCTION__, fw_type, fw_type_new);
			continue;
		}
		for (j=0; j<mac_num; j++) {
			if (oui == mac_range[j].oui) {
				if (nic >= mac_range[j].nic_start && nic <= mac_range[j].nic_end) {
					strcpy(pfw_name, mac_list[i].name);
					printf("%s: matched oui=0x%06X, nic=0x%06X\n",
						__FUNCTION__, oui, nic);
					printf("%s: fw_path=%s\n", __FUNCTION__, fw_path);
					return;
				}
			}
		}
	}
}

void
dhd_conf_set_nv_name_by_mac(dhd_pub_t *dhd, bcmsdh_info_t *sdh, char *nv_path)
{
	int i, j;
	uint8 mac[6]={0};
	int nv_num=0, mac_num=0;
	uint32 oui, nic;
	wl_mac_list_t *mac_list;
	wl_mac_range_t *mac_range;
	char *pnv_name;

	mac_list = dhd->conf->nv_by_mac.m_mac_list_head;
	nv_num = dhd->conf->nv_by_mac.count;
	if (!mac_list || !nv_num)
		return;

	if (dhd_conf_get_mac(dhd, sdh, mac)) {
		CONFIG_ERROR(("%s: Can not read MAC address\n", __FUNCTION__));
		return;
	}
	oui = (mac[0] << 16) | (mac[1] << 8) | (mac[2]);
	nic = (mac[3] << 16) | (mac[4] << 8) | (mac[5]);

	/* find out the last '/' */
	i = strlen(nv_path);
	while (i>0){
		if (nv_path[i] == '/') break;
		i--;
	}
	pnv_name = &nv_path[i+1];

	for (i=0; i<nv_num; i++) {
		mac_num = mac_list[i].count;
		mac_range = mac_list[i].mac;
		for (j=0; j<mac_num; j++) {
			if (oui == mac_range[j].oui) {
				if (nic >= mac_range[j].nic_start && nic <= mac_range[j].nic_end) {
					strcpy(pnv_name, mac_list[i].name);
					printf("%s: matched oui=0x%06X, nic=0x%06X\n",
						__FUNCTION__, oui, nic);
					printf("%s: nv_path=%s\n", __FUNCTION__, nv_path);
					return;
				}
			}
		}
	}
}

void
dhd_conf_set_fw_name_by_chip(dhd_pub_t *dhd, char *fw_path)
{
	int fw_type, ag_type;
	uint chip, chiprev;
	int i;

	chip = dhd->conf->chip;
	chiprev = dhd->conf->chiprev;

	if (fw_path[0] == '\0') {
#ifdef CONFIG_BCMDHD_FW_PATH
		bcm_strncpy_s(fw_path, MOD_PARAM_PATHLEN-1, CONFIG_BCMDHD_FW_PATH, MOD_PARAM_PATHLEN-1);
		if (fw_path[0] == '\0')
#endif
		{
			printf("firmware path is null\n");
			return;
		}
	}
#ifndef FW_PATH_AUTO_SELECT
	return;
#endif

	/* find out the last '/' */
	i = strlen(fw_path);
	while (i>0){
		if (fw_path[i] == '/') break;
		i--;
	}
#ifdef BAND_AG
	ag_type = FW_TYPE_AG;
#else
	ag_type = strstr(&fw_path[i], "_ag") ? FW_TYPE_AG : FW_TYPE_G;
#endif
	fw_type = (strstr(&fw_path[i], "_mfg") ?
		FW_TYPE_MFG : (strstr(&fw_path[i], "_apsta") ?
		FW_TYPE_APSTA : (strstr(&fw_path[i], "_p2p") ?
		FW_TYPE_P2P : FW_TYPE_STA)));

	switch (chip) {
		case BCM4330_CHIP_ID:
			if (ag_type == FW_TYPE_G) {
				if (chiprev == BCM4330B2_CHIP_REV)
					strcpy(&fw_path[i+1], bcm4330b2_fw_name[fw_type]);
				break;
			} else {
				if (chiprev == BCM4330B2_CHIP_REV)
					strcpy(&fw_path[i+1], bcm4330b2_ag_fw_name[fw_type]);
				break;
			}
		case BCM43362_CHIP_ID:
			if (chiprev == BCM43362A0_CHIP_REV)
				strcpy(&fw_path[i+1], bcm43362a0_fw_name[fw_type]);
			else
				strcpy(&fw_path[i+1], bcm43362a2_fw_name[fw_type]);
			break;
		case BCM43430_CHIP_ID:
			if (chiprev == BCM43430A0_CHIP_REV)
				strcpy(&fw_path[i+1], bcm43438a0_fw_name[fw_type]);
			break;
		case BCM43340_CHIP_ID:
			if (chiprev == BCM43340B0_CHIP_REV)
				strcpy(&fw_path[i+1], bcm43341b0_ag_fw_name[fw_type]);
			break;
		case BCM43341_CHIP_ID:
			if (chiprev == BCM43341B0_CHIP_REV)
				strcpy(&fw_path[i+1], bcm43341b0_ag_fw_name[fw_type]);
			break;
		case BCM4324_CHIP_ID:
			if (chiprev == BCM43241B4_CHIP_REV)
				strcpy(&fw_path[i+1], bcm43241b4_ag_fw_name[fw_type]);
			break;
		case BCM4335_CHIP_ID:
			if (chiprev == BCM4335A0_CHIP_REV)
				strcpy(&fw_path[i+1], bcm4339a0_ag_fw_name[fw_type]);
			break;
		case BCM4339_CHIP_ID:
			if (chiprev == BCM4339A0_CHIP_REV)
				strcpy(&fw_path[i+1], bcm4339a0_ag_fw_name[fw_type]);
			break;
		case BCM4354_CHIP_ID:
			if (chiprev == BCM4354A1_CHIP_REV)
				strcpy(&fw_path[i+1], bcm4354a1_ag_fw_name[fw_type]);
			else if (chiprev == BCM4356A2_CHIP_REV)
				strcpy(&fw_path[i+1], bcm4356a2_ag_fw_name[fw_type]);
			break;
	}

	printf("%s: firmware_path=%s\n", __FUNCTION__, fw_path);
}

#if defined(HW_OOB)
void
dhd_conf_set_hw_oob_intr(bcmsdh_info_t *sdh, uint chip)
{
	uint32 gpiocontrol, addr;

	if (CHIPID(chip) == BCM43362_CHIP_ID) {
		printf("%s: Enable HW OOB for 43362\n", __FUNCTION__);
		addr = SI_ENUM_BASE + OFFSETOF(chipcregs_t, gpiocontrol);
		gpiocontrol = bcmsdh_reg_read(sdh, addr, 4);
		gpiocontrol |= 0x2;
		bcmsdh_reg_write(sdh, addr, 4, gpiocontrol);
		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, 0x10005, 0xf, NULL);
		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, 0x10006, 0x0, NULL);
		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, 0x10007, 0x2, NULL);
	}
}
#endif

void
dhd_conf_set_fw_path(dhd_pub_t *dhd, char *fw_path)
{
	if (dhd->conf->fw_path[0]) {
		strcpy(fw_path, dhd->conf->fw_path);
		printf("%s: fw_path is changed to %s\n", __FUNCTION__, fw_path);
	}
}

void
dhd_conf_set_nv_path(dhd_pub_t *dhd, char *nv_path)
{
	if (dhd->conf->nv_path[0]) {
		strcpy(nv_path, dhd->conf->nv_path);
		printf("%s: nv_path is changed to %s\n", __FUNCTION__, nv_path);
	}
}

int
dhd_conf_set_band(dhd_pub_t *dhd)
{
	int bcmerror = -1;

	printf("%s: Set band %d\n", __FUNCTION__, dhd->conf->band);
	if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_SET_BAND, &dhd->conf->band,
		sizeof(dhd->conf->band), TRUE, 0)) < 0)
		CONFIG_ERROR(("%s: WLC_SET_BAND setting failed %d\n", __FUNCTION__, bcmerror));

	return bcmerror;
}

uint
dhd_conf_get_band(dhd_pub_t *dhd)
{
	uint band = WLC_BAND_AUTO;

	if (dhd && dhd->conf)
		band = dhd->conf->band;
	else
		CONFIG_ERROR(("%s: dhd or conf is NULL\n", __FUNCTION__));

	return band;
}

int
dhd_conf_set_country(dhd_pub_t *dhd)
{
	int bcmerror = -1;
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" + '\0' + bitvec  */

	memset(&dhd->dhd_cspec, 0, sizeof(wl_country_t));
	printf("%s: Set country %s, revision %d\n", __FUNCTION__,
		dhd->conf->cspec.ccode, dhd->conf->cspec.rev);
	bcm_mkiovar("country", (char *)&dhd->conf->cspec,
		sizeof(wl_country_t), iovbuf, sizeof(iovbuf));
	if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
		printf("%s: country code setting failed %d\n", __FUNCTION__, bcmerror);

	return bcmerror;
}

int
dhd_conf_get_country(dhd_pub_t *dhd, wl_country_t *cspec)
{
	int bcmerror = -1;

	memset(cspec, 0, sizeof(wl_country_t));
	bcm_mkiovar("country", NULL, 0, (char*)cspec, sizeof(wl_country_t));
	if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, cspec, sizeof(wl_country_t), FALSE, 0)) < 0)
		printf("%s: country code getting failed %d\n", __FUNCTION__, bcmerror);
	else
		printf("Country code: %s (%s/%d)\n", cspec->country_abbrev, cspec->ccode, cspec->rev);

	return bcmerror;
}

int
dhd_conf_fix_country(dhd_pub_t *dhd)
{
	int bcmerror = -1;
	uint band;
	wl_uint32_list_t *list;
	u8 valid_chan_list[sizeof(u32)*(WL_NUMCHANNELS + 1)];

	if (!(dhd && dhd->conf)) {
		return bcmerror;
	}

	memset(valid_chan_list, 0, sizeof(valid_chan_list));
	list = (wl_uint32_list_t *)(void *) valid_chan_list;
	list->count = htod32(WL_NUMCHANNELS);
	if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_GET_VALID_CHANNELS, valid_chan_list, sizeof(valid_chan_list), FALSE, 0)) < 0) {
		CONFIG_ERROR(("%s: get channels failed with %d\n", __FUNCTION__, bcmerror));
	}

	band = dhd_conf_get_band(dhd);

	if (bcmerror || ((band==WLC_BAND_AUTO || band==WLC_BAND_2G) &&
			dtoh32(list->count)<11)) {
		CONFIG_ERROR(("%s: bcmerror=%d, # of channels %d\n",
			__FUNCTION__, bcmerror, dtoh32(list->count)));
		if ((bcmerror = dhd_conf_set_country(dhd)) < 0) {
			strcpy(dhd->conf->cspec.country_abbrev, "US");
			dhd->conf->cspec.rev = 0;
			strcpy(dhd->conf->cspec.ccode, "US");
			dhd_conf_set_country(dhd);
		}
	}

	return bcmerror;
}

bool
dhd_conf_match_channel(dhd_pub_t *dhd, uint32 channel)
{
	int i;
	bool match = false;

	if (dhd && dhd->conf) {
		if (dhd->conf->channels.count == 0)
			return true;
		for (i=0; i<dhd->conf->channels.count; i++) {
			if (channel == dhd->conf->channels.channel[i])
				match = true;
		}
	} else {
		match = true;
		CONFIG_ERROR(("%s: dhd or conf is NULL\n", __FUNCTION__));
	}

	return match;
}

int
dhd_conf_set_roam(dhd_pub_t *dhd)
{
	int bcmerror = -1;
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" + '\0' + bitvec  */
	struct dhd_conf *conf = dhd->conf;

	printf("%s: Set roam_off %d\n", __FUNCTION__, conf->roam_off);
	dhd_roam_disable = conf->roam_off;
	bcm_mkiovar("roam_off", (char *)&conf->roam_off, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);

	if (!conf->roam_off || !conf->roam_off_suspend) {
		printf("%s: Set roam_trigger %d\n", __FUNCTION__, conf->roam_trigger[0]);
		if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_TRIGGER, conf->roam_trigger,
				sizeof(conf->roam_trigger), TRUE, 0)) < 0)
			CONFIG_ERROR(("%s: roam trigger setting failed %d\n", __FUNCTION__, bcmerror));

		printf("%s: Set roam_scan_period %d\n", __FUNCTION__, conf->roam_scan_period[0]);
		if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_SCAN_PERIOD, conf->roam_scan_period,
				sizeof(conf->roam_scan_period), TRUE, 0)) < 0)
			CONFIG_ERROR(("%s: roam scan period setting failed %d\n", __FUNCTION__, bcmerror));

		printf("%s: Set roam_delta %d\n", __FUNCTION__, conf->roam_delta[0]);
		if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_DELTA, conf->roam_delta,
				sizeof(conf->roam_delta), TRUE, 0)) < 0)
			CONFIG_ERROR(("%s: roam delta setting failed %d\n", __FUNCTION__, bcmerror));

		printf("%s: Set fullroamperiod %d\n", __FUNCTION__, conf->fullroamperiod);
		bcm_mkiovar("fullroamperiod", (char *)&conf->fullroamperiod, 4, iovbuf, sizeof(iovbuf));
		if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
			CONFIG_ERROR(("%s: roam fullscan period setting failed %d\n", __FUNCTION__, bcmerror));
	}

	return bcmerror;
}

void
dhd_conf_set_mimo_bw_cap(dhd_pub_t *dhd)
{
	int bcmerror = -1;
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" + '\0' + bitvec  */
	uint32 mimo_bw_cap;

	if (dhd->conf->mimo_bw_cap >= 0) {
		mimo_bw_cap = (uint)dhd->conf->mimo_bw_cap;
		if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0)) < 0)
			CONFIG_ERROR(("%s: WLC_DOWN setting failed %d\n", __FUNCTION__, bcmerror));
		/*  0:HT20 in ALL, 1:HT40 in ALL, 2: HT20 in 2G HT40 in 5G */
		printf("%s: Set mimo_bw_cap %d\n", __FUNCTION__, mimo_bw_cap);
		bcm_mkiovar("mimo_bw_cap", (char *)&mimo_bw_cap, 4, iovbuf, sizeof(iovbuf));
		if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
			CONFIG_ERROR(("%s: mimo_bw_cap setting failed %d\n", __FUNCTION__, bcmerror));
	}
}

void
dhd_conf_force_wme(dhd_pub_t *dhd)
{
	int bcmerror = -1;
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" + '\0' + bitvec  */

	if (dhd->conf->chip == BCM43362_CHIP_ID && dhd->conf->force_wme_ac) {
		bcm_mkiovar("force_wme_ac", (char *)&dhd->conf->force_wme_ac, 4, iovbuf, sizeof(iovbuf));
		if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
			CONFIG_ERROR(("%s: force_wme_ac setting failed %d\n", __FUNCTION__, bcmerror));
	}
}

void
dhd_conf_get_wme(dhd_pub_t *dhd, edcf_acparam_t *acp)
{
	int bcmerror = -1;
	char iovbuf[WLC_IOCTL_SMLEN];
	edcf_acparam_t *acparam;

	bzero(iovbuf, sizeof(iovbuf));

	/*
	 * Get current acparams, using buf as an input buffer.
	 * Return data is array of 4 ACs of wme params.
	 */
	bcm_mkiovar("wme_ac_sta", NULL, 0, iovbuf, sizeof(iovbuf));
	if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, iovbuf, sizeof(iovbuf), FALSE, 0)) < 0) {
		CONFIG_ERROR(("%s: wme_ac_sta getting failed %d\n", __FUNCTION__, bcmerror));
		return;
	}
	memcpy((char*)acp, iovbuf, sizeof(edcf_acparam_t)*AC_COUNT);

	acparam = &acp[AC_BK];
	CONFIG_TRACE(("%s: BK: aci %d aifsn %d ecwmin %d ecwmax %d size %d\n", __FUNCTION__,
		acparam->ACI, acparam->ACI&EDCF_AIFSN_MASK,
		acparam->ECW&EDCF_ECWMIN_MASK, (acparam->ECW&EDCF_ECWMAX_MASK)>>EDCF_ECWMAX_SHIFT,
		sizeof(acp)));
	acparam = &acp[AC_BE];
	CONFIG_TRACE(("%s: BE: aci %d aifsn %d ecwmin %d ecwmax %d size %d\n", __FUNCTION__,
		acparam->ACI, acparam->ACI&EDCF_AIFSN_MASK,
		acparam->ECW&EDCF_ECWMIN_MASK, (acparam->ECW&EDCF_ECWMAX_MASK)>>EDCF_ECWMAX_SHIFT,
		sizeof(acp)));
	acparam = &acp[AC_VI];
	CONFIG_TRACE(("%s: VI: aci %d aifsn %d ecwmin %d ecwmax %d size %d\n", __FUNCTION__,
		acparam->ACI, acparam->ACI&EDCF_AIFSN_MASK,
		acparam->ECW&EDCF_ECWMIN_MASK, (acparam->ECW&EDCF_ECWMAX_MASK)>>EDCF_ECWMAX_SHIFT,
		sizeof(acp)));
	acparam = &acp[AC_VO];
	CONFIG_TRACE(("%s: VO: aci %d aifsn %d ecwmin %d ecwmax %d size %d\n", __FUNCTION__,
		acparam->ACI, acparam->ACI&EDCF_AIFSN_MASK,
		acparam->ECW&EDCF_ECWMIN_MASK, (acparam->ECW&EDCF_ECWMAX_MASK)>>EDCF_ECWMAX_SHIFT,
		sizeof(acp)));

	return;
}

void
dhd_conf_update_wme(dhd_pub_t *dhd, edcf_acparam_t *acparam_cur, int aci)
{
	int bcmerror = -1;
	int aifsn, ecwmin, ecwmax;
	edcf_acparam_t *acp;
	char iovbuf[WLC_IOCTL_SMLEN];
	struct dhd_conf *conf = dhd->conf;

	/* Default value */
	aifsn = acparam_cur->ACI&EDCF_AIFSN_MASK;
	ecwmin = acparam_cur->ECW&EDCF_ECWMIN_MASK;
	ecwmax = (acparam_cur->ECW&EDCF_ECWMAX_MASK)>>EDCF_ECWMAX_SHIFT;

	/* Modified value */
	if (conf->wme.aifsn[aci] > 0)
		aifsn = conf->wme.aifsn[aci];
	if (conf->wme.cwmin[aci] > 0)
		ecwmin = conf->wme.cwmin[aci];
	if (conf->wme.cwmax[aci] > 0)
		ecwmax = conf->wme.cwmax[aci];

	/* Update */
	acp = acparam_cur;
	acp->ACI = (acp->ACI & ~EDCF_AIFSN_MASK) | (aifsn & EDCF_AIFSN_MASK);
	acp->ECW = ((ecwmax << EDCF_ECWMAX_SHIFT) & EDCF_ECWMAX_MASK) | (acp->ECW & EDCF_ECWMIN_MASK);
	acp->ECW = ((acp->ECW & EDCF_ECWMAX_MASK) | (ecwmin & EDCF_ECWMIN_MASK));

	CONFIG_TRACE(("%s: mod aci %d aifsn %d ecwmin %d ecwmax %d size %d\n", __FUNCTION__,
		acp->ACI, acp->ACI&EDCF_AIFSN_MASK,
		acp->ECW&EDCF_ECWMIN_MASK, (acp->ECW&EDCF_ECWMAX_MASK)>>EDCF_ECWMAX_SHIFT,
		sizeof(edcf_acparam_t)));

	/*
	* Now use buf as an output buffer.
	* Put WME acparams after "wme_ac\0" in buf.
	* NOTE: only one of the four ACs can be set at a time.
	*/
	bcm_mkiovar("wme_ac_sta", (char*)acp, sizeof(edcf_acparam_t), iovbuf,
		sizeof(iovbuf));
	if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), FALSE, 0)) < 0) {
		CONFIG_ERROR(("%s: wme_ac_sta setting failed %d\n", __FUNCTION__, bcmerror));
		return;
	}
}

void
dhd_conf_set_wme(dhd_pub_t *dhd)
{
	edcf_acparam_t acparam_cur[AC_COUNT];

	if (dhd && dhd->conf) {
		if (!dhd->conf->force_wme_ac) {
			CONFIG_TRACE(("%s: force_wme_ac is not enabled %d\n",
				__FUNCTION__, dhd->conf->force_wme_ac));
			return;
		}

		CONFIG_TRACE(("%s: Before change:\n", __FUNCTION__));
		dhd_conf_get_wme(dhd, acparam_cur);

		dhd_conf_update_wme(dhd, &acparam_cur[AC_BK], AC_BK);
		dhd_conf_update_wme(dhd, &acparam_cur[AC_BE], AC_BE);
		dhd_conf_update_wme(dhd, &acparam_cur[AC_VI], AC_VI);
		dhd_conf_update_wme(dhd, &acparam_cur[AC_VO], AC_VO);

		CONFIG_TRACE(("%s: After change:\n", __FUNCTION__));
		dhd_conf_get_wme(dhd, acparam_cur);
	} else {
		CONFIG_ERROR(("%s: dhd or conf is NULL\n", __FUNCTION__));
	}

	return;
}

void
dhd_conf_set_stbc(dhd_pub_t *dhd)
{
	int bcmerror = -1;
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" + '\0' + bitvec  */
	uint stbc = 0;

	if (dhd->conf->chip == BCM4324_CHIP_ID && dhd->conf->stbc >= 0) {
		stbc = (uint)dhd->conf->stbc;
		printf("%s: set stbc_tx %d\n", __FUNCTION__, stbc);
		bcm_mkiovar("stbc_tx", (char *)&stbc, 4, iovbuf, sizeof(iovbuf));
		if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
			CONFIG_ERROR(("%s: stbc_tx setting failed %d\n", __FUNCTION__, bcmerror));

		printf("%s: set stbc_rx %d\n", __FUNCTION__, stbc);
		bcm_mkiovar("stbc_rx", (char *)&stbc, 4, iovbuf, sizeof(iovbuf));
		if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
			CONFIG_ERROR(("%s: stbc_rx setting failed %d\n", __FUNCTION__, bcmerror));
	}
}

void
dhd_conf_set_phyoclscdenable(dhd_pub_t *dhd)
{
	int bcmerror = -1;
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" + '\0' + bitvec  */
	uint phy_oclscdenable = 0;

	if (dhd->conf->chip == BCM4324_CHIP_ID && dhd->conf->phy_oclscdenable >= 0) {
		phy_oclscdenable = (uint)dhd->conf->phy_oclscdenable;
		printf("%s: set stbc_tx %d\n", __FUNCTION__, phy_oclscdenable);
		bcm_mkiovar("phy_oclscdenable", (char *)&phy_oclscdenable, 4, iovbuf, sizeof(iovbuf));
		if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
			CONFIG_ERROR(("%s: stbc_tx setting failed %d\n", __FUNCTION__, bcmerror));
	}
}

#ifdef PKT_FILTER_SUPPORT
void
dhd_conf_add_pkt_filter(dhd_pub_t *dhd)
{
	int i;

	/*
	All pkt: pkt_filter_add=99 0 0 0 0x000000000000 0x000000000000
	Netbios pkt: 120 0 0 12 0xFFFF000000000000000000FF000000000000000000000000FFFF 0x0800000000000000000000110000000000000000000000000089
	*/
	for(i=0; i<dhd->conf->pkt_filter_add.count; i++) {
		dhd->pktfilter[i+dhd->pktfilter_count] = dhd->conf->pkt_filter_add.filter[i];
		printf("%s: %s\n", __FUNCTION__, dhd->pktfilter[i+dhd->pktfilter_count]);
	}
	dhd->pktfilter_count += i;
}

bool
dhd_conf_del_pkt_filter(dhd_pub_t *dhd, uint32 id)
{
	int i;

	for(i=0; i<dhd->conf->pkt_filter_del.count; i++) {
		if (id == dhd->conf->pkt_filter_del.id[i]) {
			printf("%s: %d\n", __FUNCTION__, dhd->conf->pkt_filter_del.id[i]);
			return true;
		}
	}
	return false;
}

void
dhd_conf_discard_pkt_filter(dhd_pub_t *dhd)
{
	dhd->pktfilter[DHD_UNICAST_FILTER_NUM] = NULL;
	dhd->pktfilter[DHD_BROADCAST_FILTER_NUM] = "101 0 0 0 0xFFFFFFFFFFFF 0xFFFFFFFFFFFF";
	dhd->pktfilter[DHD_MULTICAST4_FILTER_NUM] = "102 0 0 0 0xFFFFFF 0x01005E";
	dhd->pktfilter[DHD_MULTICAST6_FILTER_NUM] = "103 0 0 0 0xFFFF 0x3333";
	dhd->pktfilter[DHD_MDNS_FILTER_NUM] = NULL;
	/* Do not enable ARP to pkt filter if dhd_master_mode is false.*/
	dhd->pktfilter[DHD_ARP_FILTER_NUM] = NULL;

	/* IPv4 broadcast address XXX.XXX.XXX.255 */
	dhd->pktfilter[dhd->pktfilter_count] = "110 0 0 12 0xFFFF00000000000000000000000000000000000000FF 0x080000000000000000000000000000000000000000FF";
	dhd->pktfilter_count++;
	/* discard IPv4 multicast address 224.0.0.0/4 */
	dhd->pktfilter[dhd->pktfilter_count] = "111 0 0 12 0xFFFF00000000000000000000000000000000F0 0x080000000000000000000000000000000000E0";
	dhd->pktfilter_count++;
	/* discard IPv6 multicast address FF00::/8 */
	dhd->pktfilter[dhd->pktfilter_count] = "112 0 0 12 0xFFFF000000000000000000000000000000000000000000000000FF 0x86DD000000000000000000000000000000000000000000000000FF";
	dhd->pktfilter_count++;
	/* discard Netbios pkt */
	dhd->pktfilter[dhd->pktfilter_count] = "120 0 0 12 0xFFFF000000000000000000FF000000000000000000000000FFFF 0x0800000000000000000000110000000000000000000000000089";
	dhd->pktfilter_count++;

}
#endif /* PKT_FILTER_SUPPORT */

void
dhd_conf_set_srl(dhd_pub_t *dhd)
{
	int bcmerror = -1;
	uint srl = 0;

	if (dhd->conf->srl >= 0) {
		srl = (uint)dhd->conf->srl;
		printf("%s: set srl %d\n", __FUNCTION__, srl);
		if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_SET_SRL, &srl , sizeof(srl), true, 0)) < 0)
			CONFIG_ERROR(("%s: WLC_SET_SRL setting failed %d\n", __FUNCTION__, bcmerror));
	}
}

void
dhd_conf_set_lrl(dhd_pub_t *dhd)
{
	int bcmerror = -1;
	uint lrl = 0;

	if (dhd->conf->lrl >= 0) {
		lrl = (uint)dhd->conf->lrl;
		printf("%s: set lrl %d\n", __FUNCTION__, lrl);
		if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_SET_LRL, &lrl , sizeof(lrl), true, 0)) < 0)
			CONFIG_ERROR(("%s: WLC_SET_LRL setting failed %d\n", __FUNCTION__, bcmerror));
	}
}

void
dhd_conf_set_glom(dhd_pub_t *dhd)
{
	int bcmerror = -1;
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" + '\0' + bitvec  */
	uint32 bus_txglom = 0;

	if (dhd->conf->bus_txglom) {
		bus_txglom = (uint)dhd->conf->bus_txglom;
		printf("%s: set bus:txglom %d\n", __FUNCTION__, bus_txglom);
		bcm_mkiovar("bus:txglom", (char *)&bus_txglom, 4, iovbuf, sizeof(iovbuf));
		dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
		if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
			CONFIG_ERROR(("%s: bus:txglom setting failed %d\n", __FUNCTION__, bcmerror));
	}
}

void
dhd_conf_set_ampdu_ba_wsize(dhd_pub_t *dhd)
{
	int bcmerror = -1;
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" + '\0' + bitvec  */
	uint32 ampdu_ba_wsize = dhd->conf->ampdu_ba_wsize;

	/* Set ampdu ba wsize */
	if (ampdu_ba_wsize > 0) {
		printf("%s: set ampdu_ba_wsize %d\n", __FUNCTION__, ampdu_ba_wsize);
		bcm_mkiovar("ampdu_ba_wsize", (char *)&ampdu_ba_wsize, 4, iovbuf, sizeof(iovbuf));
		if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
				sizeof(iovbuf), TRUE, 0)) < 0) {
			DHD_ERROR(("%s Set ampdu_ba_wsize to %d failed	%d\n",
				__FUNCTION__, ampdu_ba_wsize, bcmerror));
		}
	}
}

void
dhd_conf_set_spect(dhd_pub_t *dhd)
{
	int bcmerror = -1;
	uint spect = 0;

	if (dhd->conf->spect >= 0) {
		spect = (uint)dhd->conf->spect;
		if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0)) < 0)
			CONFIG_ERROR(("%s: WLC_DOWN setting failed %d\n", __FUNCTION__, bcmerror));
		printf("%s: set spect %d\n", __FUNCTION__, spect);
		if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_SET_SPECT_MANAGMENT, &spect , sizeof(spect), true, 0)) < 0)
			CONFIG_ERROR(("%s: WLC_SET_SPECT_MANAGMENT setting failed %d\n", __FUNCTION__, bcmerror));
	}
}

unsigned int
process_config_vars(char *varbuf, unsigned int len, char *pickbuf, char *param)
{
	bool findNewline, changenewline=FALSE, pick=FALSE;
	int column;
	unsigned int n, pick_column=0;

	findNewline = FALSE;
	column = 0;

	for (n = 0; n < len; n++) {
		if (varbuf[n] == '\r')
			continue;
		if ((findNewline || changenewline) && varbuf[n] != '\n')
			continue;
		findNewline = FALSE;
		if (varbuf[n] == '#') {
			findNewline = TRUE;
			continue;
		}
		if (varbuf[n] == '\\') {
			changenewline = TRUE;
			continue;
		}
		if (!changenewline && varbuf[n] == '\n') {
			if (column == 0)
				continue;
			column = 0;
			continue;
		}
		if (changenewline && varbuf[n] == '\n') {
			changenewline = FALSE;
			continue;
		}
		if (!memcmp(&varbuf[n], param, strlen(param)) && column==0) {
			pick = TRUE;
			column = strlen(param);
			n += column;
			pick_column = 0;
		} else {
			if (pick && column==0)
				pick = FALSE;
			else
				column++;
		}
		if (pick) {
			if (varbuf[n] == 0x9)
				continue;
			if (pick_column>0 && pickbuf[pick_column-1]==' ' && varbuf[n]==' ')
				continue;
			pickbuf[pick_column] = varbuf[n];
			pick_column++;
		}
	}

	return pick_column;
}

void
dhd_conf_read_log_level(dhd_pub_t *dhd, char *bufp, uint len)
{
	uint len_val;
	char pick[MAXSZ_BUF];

	/* Process dhd_msglevel */
	memset(pick, 0, MAXSZ_BUF);
	len_val = process_config_vars(bufp, len, pick, "msglevel=");
	if (len_val) {
		dhd_msg_level = (int)simple_strtol(pick, NULL, 0);
		printf("%s: dhd_msg_level = 0x%X\n", __FUNCTION__, dhd_msg_level);
	}
	/* Process sd_msglevel */
	memset(pick, 0, MAXSZ_BUF);
	len_val = process_config_vars(bufp, len, pick, "sd_msglevel=");
	if (len_val) {
		sd_msglevel = (int)simple_strtol(pick, NULL, 0);
		printf("%s: sd_msglevel = 0x%X\n", __FUNCTION__, sd_msglevel);
	}
	/* Process android_msg_level */
	memset(pick, 0, MAXSZ_BUF);
	len_val = process_config_vars(bufp, len, pick, "android_msg_level=");
	if (len_val) {
		android_msg_level = (int)simple_strtol(pick, NULL, 0);
		printf("%s: android_msg_level = 0x%X\n", __FUNCTION__, android_msg_level);
	}
	/* Process config_msg_level */
	memset(pick, 0, MAXSZ_BUF);
	len_val = process_config_vars(bufp, len, pick, "config_msg_level=");
	if (len_val) {
		config_msg_level = (int)simple_strtol(pick, NULL, 0);
		printf("%s: config_msg_level = 0x%X\n", __FUNCTION__, config_msg_level);
	}
#ifdef WL_CFG80211
	/* Process wl_dbg_level */
	memset(pick, 0, MAXSZ_BUF);
	len_val = process_config_vars(bufp, len, pick, "wl_dbg_level=");
	if (len_val) {
		wl_dbg_level = (int)simple_strtol(pick, NULL, 0);
		printf("%s: wl_dbg_level = 0x%X\n", __FUNCTION__, wl_dbg_level);
	}
#endif
#if defined(WL_WIRELESS_EXT)
	/* Process iw_msg_level */
	memset(pick, 0, MAXSZ_BUF);
	len_val = process_config_vars(bufp, len, pick, "iw_msg_level=");
	if (len_val) {
		iw_msg_level = (int)simple_strtol(pick, NULL, 0);
		printf("%s: iw_msg_level = 0x%X\n", __FUNCTION__, iw_msg_level);
	}
#endif

	/* Process dhd_console_ms */
	memset(pick, 0, MAXSZ_BUF);
	len_val = process_config_vars(bufp, len, pick, "dhd_console_ms=");
	if (len_val) {
		dhd_console_ms = (int)simple_strtol(pick, NULL, 0);
		printf("%s: dhd_console_ms = 0x%X\n", __FUNCTION__, dhd_console_ms);
	}
}

void
dhd_conf_read_wme_ac_params(dhd_pub_t *dhd, char *bufp, uint len)
{
	uint len_val;
	char pick[MAXSZ_BUF];
	struct dhd_conf *conf = dhd->conf;

	/* Process WMM parameters */
	memset(pick, 0, MAXSZ_BUF);
	len_val = process_config_vars(bufp, len, pick, "force_wme_ac=");
	if (len_val) {
		conf->force_wme_ac = (int)simple_strtol(pick, NULL, 10);
		printf("%s: force_wme_ac = %d\n", __FUNCTION__, conf->force_wme_ac);
	}

	if (conf->force_wme_ac) {
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "bk_aifsn=");
		if (len_val) {
			conf->wme.aifsn[AC_BK] = (int)simple_strtol(pick, NULL, 10);
			printf("%s: AC_BK aifsn = %d\n", __FUNCTION__, conf->wme.aifsn[AC_BK]);
		}

		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "bk_cwmin=");
		if (len_val) {
			conf->wme.cwmin[AC_BK] = (int)simple_strtol(pick, NULL, 10);
			printf("%s: AC_BK cwmin = %d\n", __FUNCTION__, conf->wme.cwmin[AC_BK]);
		}

		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "bk_cwmax=");
		if (len_val) {
			conf->wme.cwmax[AC_BK] = (int)simple_strtol(pick, NULL, 10);
			printf("%s: AC_BK cwmax = %d\n", __FUNCTION__, conf->wme.cwmax[AC_BK]);
		}

		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "be_aifsn=");
		if (len_val) {
			conf->wme.aifsn[AC_BE] = (int)simple_strtol(pick, NULL, 10);
			printf("%s: AC_BE aifsn = %d\n", __FUNCTION__, conf->wme.aifsn[AC_BE]);
		}

		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "be_cwmin=");
		if (len_val) {
			conf->wme.cwmin[AC_BE] = (int)simple_strtol(pick, NULL, 10);
			printf("%s: AC_BE cwmin = %d\n", __FUNCTION__, conf->wme.cwmin[AC_BE]);
		}

		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "be_cwmax=");
		if (len_val) {
			conf->wme.cwmax[AC_BE] = (int)simple_strtol(pick, NULL, 10);
			printf("%s: AC_BE cwmax = %d\n", __FUNCTION__, conf->wme.cwmax[AC_BE]);
		}

		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "vi_aifsn=");
		if (len_val) {
			conf->wme.aifsn[AC_VI] = (int)simple_strtol(pick, NULL, 10);
			printf("%s: AC_VI aifsn = %d\n", __FUNCTION__, conf->wme.aifsn[AC_VI]);
		}

		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "vi_cwmin=");
		if (len_val) {
			conf->wme.cwmin[AC_VI] = (int)simple_strtol(pick, NULL, 10);
			printf("%s: AC_VI cwmin = %d\n", __FUNCTION__, conf->wme.cwmin[AC_VI]);
		}

		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "vi_cwmax=");
		if (len_val) {
			conf->wme.cwmax[AC_VI] = (int)simple_strtol(pick, NULL, 10);
			printf("%s: AC_VI cwmax = %d\n", __FUNCTION__, conf->wme.cwmax[AC_VI]);
		}

		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "vo_aifsn=");
		if (len_val) {
			conf->wme.aifsn[AC_VO] = (int)simple_strtol(pick, NULL, 10);
			printf("%s: AC_VO aifsn = %d\n", __FUNCTION__, conf->wme.aifsn[AC_VO]);
		}

		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "vo_cwmin=");
		if (len_val) {
			conf->wme.cwmin[AC_VO] = (int)simple_strtol(pick, NULL, 10);
			printf("%s: AC_VO cwmin = %d\n", __FUNCTION__, conf->wme.cwmin[AC_VO]);
		}

		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "vo_cwmax=");
		if (len_val) {
			conf->wme.cwmax[AC_VO] = (int)simple_strtol(pick, NULL, 10);
			printf("%s: AC_VO cwmax = %d\n", __FUNCTION__, conf->wme.cwmax[AC_VO]);
		}
	}

}

void
dhd_conf_read_roam_params(dhd_pub_t *dhd, char *bufp, uint len)
{
	uint len_val;
	char pick[MAXSZ_BUF];
	struct dhd_conf *conf = dhd->conf;
	
	/* Process roam */
	memset(pick, 0, MAXSZ_BUF);
	len_val = process_config_vars(bufp, len, pick, "roam_off=");
	if (len_val) {
		if (!strncmp(pick, "0", len_val))
			conf->roam_off = 0;
		else
			conf->roam_off = 1;
		printf("%s: roam_off = %d\n", __FUNCTION__, conf->roam_off);
	}

	memset(pick, 0, MAXSZ_BUF);
	len_val = process_config_vars(bufp, len, pick, "roam_off_suspend=");
	if (len_val) {
		if (!strncmp(pick, "0", len_val))
			conf->roam_off_suspend = 0;
		else
			conf->roam_off_suspend = 1;
		printf("%s: roam_off_suspend = %d\n", __FUNCTION__,
			conf->roam_off_suspend);
	}

	if (!conf->roam_off || !conf->roam_off_suspend) {
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "roam_trigger=");
		if (len_val)
			conf->roam_trigger[0] = (int)simple_strtol(pick, NULL, 10);
		printf("%s: roam_trigger = %d\n", __FUNCTION__,
			conf->roam_trigger[0]);

		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "roam_scan_period=");
		if (len_val)
			conf->roam_scan_period[0] = (int)simple_strtol(pick, NULL, 10);
		printf("%s: roam_scan_period = %d\n", __FUNCTION__,
			conf->roam_scan_period[0]);

		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "roam_delta=");
		if (len_val)
			conf->roam_delta[0] = (int)simple_strtol(pick, NULL, 10);
		printf("%s: roam_delta = %d\n", __FUNCTION__, conf->roam_delta[0]);

		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "fullroamperiod=");
		if (len_val)
			conf->fullroamperiod = (int)simple_strtol(pick, NULL, 10);
		printf("%s: fullroamperiod = %d\n", __FUNCTION__,
			conf->fullroamperiod);
	}

}

/*
 * [fw_by_mac]:
 * fw_by_mac=[fw_mac_num] \
 *  [fw_name1] [mac_num1] [oui1-1] [nic_start1-1] [nic_end1-1] \
 *                                    [oui1-1] [nic_start1-1] [nic_end1-1]... \
 *                                    [oui1-n] [nic_start1-n] [nic_end1-n] \
 *  [fw_name2] [mac_num2] [oui2-1] [nic_start2-1] [nic_end2-1] \
 *                                    [oui2-1] [nic_start2-1] [nic_end2-1]... \
 *                                    [oui2-n] [nic_start2-n] [nic_end2-n] \
 * Ex: fw_by_mac=2 \
 *  fw_bcmdhd1.bin 2 0x0022F4 0xE85408 0xE8549D 0x983B16 0x3557A9 0x35582A \
 *  fw_bcmdhd2.bin 3 0x0022F4 0xE85408 0xE8549D 0x983B16 0x3557A9 0x35582A \
 *                           0x983B16 0x916157 0x916487
 * [nv_by_mac]: The same format as fw_by_mac
 *
*/
int
dhd_conf_read_config(dhd_pub_t *dhd)
{
	int bcmerror = -1, i, j;
	uint len, len_val;
	void * image = NULL;
	char * memblock = NULL;
	char *bufp, pick[MAXSZ_BUF], *pch, *pick_tmp;
	char *pconf_path;
	bool conf_file_exists;
	wl_mac_list_t *mac_list;
	wl_mac_range_t *mac_range;
	struct dhd_conf *conf = dhd->conf;

	pconf_path = dhd->conf_path;

	conf_file_exists = ((pconf_path != NULL) && (pconf_path[0] != '\0'));
	if (!conf_file_exists)
		return (0);

	if (conf_file_exists) {
		image = dhd_os_open_image(pconf_path);
		if (image == NULL) {
			printk("%s: Ignore config file %s\n", __FUNCTION__, pconf_path);
			goto err;
		}
	}

	memblock = MALLOC(dhd->osh, MAXSZ_CONFIG);
	if (memblock == NULL) {
		CONFIG_ERROR(("%s: Failed to allocate memory %d bytes\n",
			__FUNCTION__, MAXSZ_CONFIG));
		goto err;
	}

	/* Read variables */
	if (conf_file_exists) {
		len = dhd_os_get_image_block(memblock, MAXSZ_CONFIG, image);
	}
	if (len > 0 && len < MAXSZ_CONFIG) {
		bufp = (char *)memblock;
		bufp[len] = 0;

		/* Process log_level */
		dhd_conf_read_log_level(dhd, bufp, len);
		dhd_conf_read_roam_params(dhd, bufp, len);
		dhd_conf_read_wme_ac_params(dhd, bufp, len);

		/* Process fw_by_mac */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "fw_by_mac=");
		if (len_val) {
			pick_tmp = pick;
			pch = bcmstrtok(&pick_tmp, " ", 0);
			conf->fw_by_mac.count = (uint32)simple_strtol(pch, NULL, 0);
			if (!(mac_list = kmalloc(sizeof(wl_mac_list_t)*conf->fw_by_mac.count, GFP_KERNEL))) {
				conf->fw_by_mac.count = 0;
				CONFIG_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
			}
			printf("%s: fw_count=%d\n", __FUNCTION__, conf->fw_by_mac.count);
			conf->fw_by_mac.m_mac_list_head = mac_list;
			for (i=0; i<conf->fw_by_mac.count; i++) {
				pch = bcmstrtok(&pick_tmp, " ", 0);
				strcpy(mac_list[i].name, pch);
				pch = bcmstrtok(&pick_tmp, " ", 0);
				mac_list[i].count = (uint32)simple_strtol(pch, NULL, 0);
				printf("%s: name=%s, mac_count=%d\n", __FUNCTION__,
					mac_list[i].name, mac_list[i].count);
				if (!(mac_range = kmalloc(sizeof(wl_mac_range_t)*mac_list[i].count, GFP_KERNEL))) {
					mac_list[i].count = 0;
					CONFIG_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
					break;
				}
				mac_list[i].mac = mac_range;
				for (j=0; j<mac_list[i].count; j++) {
					pch = bcmstrtok(&pick_tmp, " ", 0);
					mac_range[j].oui = (uint32)simple_strtol(pch, NULL, 0);
					pch = bcmstrtok(&pick_tmp, " ", 0);
					mac_range[j].nic_start = (uint32)simple_strtol(pch, NULL, 0);
					pch = bcmstrtok(&pick_tmp, " ", 0);
					mac_range[j].nic_end = (uint32)simple_strtol(pch, NULL, 0);
					printf("%s: oui=0x%06X, nic_start=0x%06X, nic_end=0x%06X\n",
						__FUNCTION__, mac_range[j].oui,
						mac_range[j].nic_start, mac_range[j].nic_end);
				}
			}
		}

		/* Process nv_by_mac */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "nv_by_mac=");
		if (len_val) {
			pick_tmp = pick;
			pch = bcmstrtok(&pick_tmp, " ", 0);
			conf->nv_by_mac.count = (uint32)simple_strtol(pch, NULL, 0);
			if (!(mac_list = kmalloc(sizeof(wl_mac_list_t)*conf->nv_by_mac.count, GFP_KERNEL))) {
				conf->nv_by_mac.count = 0;
				CONFIG_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
			}
			printf("%s: nv_count=%d\n", __FUNCTION__, conf->nv_by_mac.count);
			conf->nv_by_mac.m_mac_list_head = mac_list;
			for (i=0; i<conf->nv_by_mac.count; i++) {
				pch = bcmstrtok(&pick_tmp, " ", 0);
				strcpy(mac_list[i].name, pch);
				pch = bcmstrtok(&pick_tmp, " ", 0);
				mac_list[i].count = (uint32)simple_strtol(pch, NULL, 0);
				printf("%s: name=%s, mac_count=%d\n", __FUNCTION__,
					mac_list[i].name, mac_list[i].count);
				if (!(mac_range = kmalloc(sizeof(wl_mac_range_t)*mac_list[i].count, GFP_KERNEL))) {
					mac_list[i].count = 0;
					CONFIG_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
					break;
				}
				mac_list[i].mac = mac_range;
				for (j=0; j<mac_list[i].count; j++) {
					pch = bcmstrtok(&pick_tmp, " ", 0);
					mac_range[j].oui = (uint32)simple_strtol(pch, NULL, 0);
					pch = bcmstrtok(&pick_tmp, " ", 0);
					mac_range[j].nic_start = (uint32)simple_strtol(pch, NULL, 0);
					pch = bcmstrtok(&pick_tmp, " ", 0);
					mac_range[j].nic_end = (uint32)simple_strtol(pch, NULL, 0);
					printf("%s: oui=0x%06X, nic_start=0x%06X, nic_end=0x%06X\n",
						__FUNCTION__, mac_range[j].oui,
						mac_range[j].nic_start, mac_range[j].nic_end);
				}
			}
		}

		/* Process firmware path */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "fw_path=");
		if (len_val) {
			memcpy(conf->fw_path, pick, len_val);
			printf("%s: fw_path = %s\n", __FUNCTION__, conf->fw_path);
		}

		/* Process nvram path */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "nv_path=");
		if (len_val) {
			memcpy(conf->nv_path, pick, len_val);
			printf("%s: nv_path = %s\n", __FUNCTION__, conf->nv_path);
		}

		/* Process band */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "band=");
		if (len_val) {
			if (!strncmp(pick, "b", len_val))
				conf->band = WLC_BAND_2G;
			else if (!strncmp(pick, "a", len_val))
				conf->band = WLC_BAND_5G;
			else
				conf->band = WLC_BAND_AUTO;
			printf("%s: band = %d\n", __FUNCTION__, conf->band);
		}

		/* Process bandwidth */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "mimo_bw_cap=");
		if (len_val) {
			conf->mimo_bw_cap = (uint)simple_strtol(pick, NULL, 10);
			printf("%s: mimo_bw_cap = %d\n", __FUNCTION__, conf->mimo_bw_cap);
		}

		/* Process country code */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "ccode=");
		if (len_val) {
			memset(&conf->cspec, 0, sizeof(wl_country_t));
			memcpy(conf->cspec.country_abbrev, pick, len_val);
			memcpy(conf->cspec.ccode, pick, len_val);
			memset(pick, 0, MAXSZ_BUF);
			len_val = process_config_vars(bufp, len, pick, "regrev=");
			if (len_val)
				conf->cspec.rev = (int32)simple_strtol(pick, NULL, 10);
		}

		/* Process channels */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "channels=");
		pick_tmp = pick;
		if (len_val) {
			pch = bcmstrtok(&pick_tmp, " ,.-", 0);
			i=0;
			while (pch != NULL && i<WL_NUMCHANNELS) {
				conf->channels.channel[i] = (uint32)simple_strtol(pch, NULL, 10);
				pch = bcmstrtok(&pick_tmp, " ,.-", 0);
				i++;
			}
			conf->channels.count = i;
			printf("%s: channels = ", __FUNCTION__);
			for (i=0; i<conf->channels.count; i++)
				printf("%d ", conf->channels.channel[i]);
			printf("\n");
		}

		/* Process keep alive period */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "keep_alive_period=");
		if (len_val) {
			conf->keep_alive_period = (int)simple_strtol(pick, NULL, 10);
			printf("%s: keep_alive_period = %d\n", __FUNCTION__,
				conf->keep_alive_period);
		}

		/* Process STBC parameters */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "stbc=");
		if (len_val) {
			conf->stbc = (int)simple_strtol(pick, NULL, 10);
			printf("%s: stbc = %d\n", __FUNCTION__, conf->stbc);
		}

		/* Process phy_oclscdenable parameters */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "phy_oclscdenable=");
		if (len_val) {
			conf->phy_oclscdenable = (int)simple_strtol(pick, NULL, 10);
			printf("%s: phy_oclscdenable = %d\n", __FUNCTION__, conf->phy_oclscdenable);
		}

		/* Process dhd_doflow parameters */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "dhd_doflow=");
		if (len_val) {
			if (!strncmp(pick, "0", len_val))
				dhd_doflow = FALSE;
			else
				dhd_doflow = TRUE;
			printf("%s: dhd_doflow = %d\n", __FUNCTION__, dhd_doflow);
		}

		/* Process dhd_master_mode parameters */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "dhd_master_mode=");
		if (len_val) {
			if (!strncmp(pick, "0", len_val))
				dhd_master_mode = FALSE;
			else
				dhd_master_mode = TRUE;
			printf("%s: dhd_master_mode = %d\n", __FUNCTION__, dhd_master_mode);
		}

		/* Process pkt_filter_add */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "pkt_filter_add=");
		pick_tmp = pick;
		if (len_val) {
			pch = bcmstrtok(&pick_tmp, ",.-", 0);
			i=0;
			while (pch != NULL && i<DHD_CONF_FILTER_MAX) {
				strcpy(&conf->pkt_filter_add.filter[i][0], pch);
				printf("%s: pkt_filter_add[%d][] = %s\n", __FUNCTION__, i, &conf->pkt_filter_add.filter[i][0]);
				pch = bcmstrtok(&pick_tmp, ",.-", 0);
				i++;
			}
			conf->pkt_filter_add.count = i;
		}

		/* Process pkt_filter_del */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "pkt_filter_del=");
		pick_tmp = pick;
		if (len_val) {
			pch = bcmstrtok(&pick_tmp, " ,.-", 0);
			i=0;
			while (pch != NULL && i<DHD_CONF_FILTER_MAX) {
				conf->pkt_filter_del.id[i] = (uint32)simple_strtol(pch, NULL, 10);
				pch = bcmstrtok(&pick_tmp, " ,.-", 0);
				i++;
			}
			conf->pkt_filter_del.count = i;
			printf("%s: pkt_filter_del id = ", __FUNCTION__);
			for (i=0; i<conf->pkt_filter_del.count; i++)
				printf("%d ", conf->pkt_filter_del.id[i]);
			printf("\n");
		}

		/* Process srl parameters */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "srl=");
		if (len_val) {
			conf->srl = (int)simple_strtol(pick, NULL, 10);
			printf("%s: srl = %d\n", __FUNCTION__, conf->srl);
		}

		/* Process lrl parameters */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "lrl=");
		if (len_val) {
			conf->lrl = (int)simple_strtol(pick, NULL, 10);
			printf("%s: lrl = %d\n", __FUNCTION__, conf->lrl);
		}

		/* Process beacon timeout parameters */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "bcn_timeout=");
		if (len_val) {
			conf->bcn_timeout= (int)simple_strtol(pick, NULL, 10);
			printf("%s: bcn_timeout = %d\n", __FUNCTION__, conf->bcn_timeout);
		}

		/* Process bus_txglom */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "bus:txglom=");
		if (len_val) {
			conf->bus_txglom = (int)simple_strtol(pick, NULL, 10);
			printf("%s: bus:txglom = %d\n", __FUNCTION__, conf->bus_txglom);
		}

		/* Process ampdu_ba_wsize parameters */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "ampdu_ba_wsize=");
		if (len_val) {
			conf->ampdu_ba_wsize = (int)simple_strtol(pick, NULL, 10);
			printf("%s: ampdu_ba_wsize = %d\n", __FUNCTION__, conf->ampdu_ba_wsize);
		}

		/* Process kso parameters */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "kso_enable=");
		if (len_val) {
			if (!strncmp(pick, "0", len_val))
				conf->kso_enable = FALSE;
			else
				conf->kso_enable = TRUE;
			printf("%s: kso_enable = %d\n", __FUNCTION__, conf->kso_enable);
		}

		/* Process spect parameters */
		memset(pick, 0, MAXSZ_BUF);
		len_val = process_config_vars(bufp, len, pick, "spect=");
		if (len_val) {
			conf->spect = (int)simple_strtol(pick, NULL, 10);
			printf("%s: spect = %d\n", __FUNCTION__, conf->spect);
		}
 
		bcmerror = 0;
	} else {
		CONFIG_ERROR(("%s: error reading config file: %d\n", __FUNCTION__, len));
		bcmerror = BCME_SDIO_ERROR;
	}

err:
	if (memblock)
		MFREE(dhd->osh, memblock, MAXSZ_CONFIG);

	if (image)
		dhd_os_close_image(image);

	return bcmerror;
}

int
dhd_conf_set_chiprev(dhd_pub_t *dhd, uint chip, uint chiprev)
{
	dhd->conf->chip = chip;
	dhd->conf->chiprev = chiprev;
	return 0;
}

uint
dhd_conf_get_chip(void *context)
{
	dhd_pub_t *dhd = context;

	if (dhd && dhd->conf)
		return dhd->conf->chip;
	return 0;
}

uint
dhd_conf_get_chiprev(void *context)
{
	dhd_pub_t *dhd = context;

	if (dhd && dhd->conf)
		return dhd->conf->chiprev;
	return 0;
}

int
dhd_conf_preinit(dhd_pub_t *dhd)
{
	struct dhd_conf *conf = dhd->conf;

	CONFIG_TRACE(("%s: Enter\n", __FUNCTION__));

	dhd_conf_free_mac_list(&conf->fw_by_mac);
	dhd_conf_free_mac_list(&conf->nv_by_mac);
	conf->band = WLC_BAND_AUTO;
	conf->mimo_bw_cap = -1;
	if (conf->chip == BCM43362_CHIP_ID || conf->chip == BCM4330_CHIP_ID) {
		strcpy(conf->cspec.country_abbrev, "ALL");
		strcpy(conf->cspec.ccode, "ALL");
		conf->cspec.rev = 0;
	} else if (conf->chip == BCM4335_CHIP_ID || conf->chip == BCM4339_CHIP_ID ||
			conf->chip == BCM4354_CHIP_ID) {
		strcpy(conf->cspec.country_abbrev, "CN");
		strcpy(conf->cspec.ccode, "CN");
		conf->cspec.rev = 38;
	} else {
		strcpy(conf->cspec.country_abbrev, "CN");
		strcpy(conf->cspec.ccode, "CN");
		conf->cspec.rev = 0;
	}
	memset(&conf->channels, 0, sizeof(wl_channel_list_t));
	conf->roam_off = 1;
	conf->roam_off_suspend = 1;
#ifdef CUSTOM_ROAM_TRIGGER_SETTING
	conf->roam_trigger[0] = CUSTOM_ROAM_TRIGGER_SETTING;
#else
	conf->roam_trigger[0] = -65;
#endif
	conf->roam_trigger[1] = WLC_BAND_ALL;
	conf->roam_scan_period[0] = 10;
	conf->roam_scan_period[1] = WLC_BAND_ALL;
#ifdef CUSTOM_ROAM_DELTA_SETTING
	conf->roam_delta[0] = CUSTOM_ROAM_DELTA_SETTING;
#else
	conf->roam_delta[0] = 15;
#endif
	conf->roam_delta[1] = WLC_BAND_ALL;
#ifdef FULL_ROAMING_SCAN_PERIOD_60_SEC
	conf->fullroamperiod = 60;
#else /* FULL_ROAMING_SCAN_PERIOD_60_SEC */
	conf->fullroamperiod = 120;
#endif /* FULL_ROAMING_SCAN_PERIOD_60_SEC */
#ifdef CUSTOM_KEEP_ALIVE_SETTING
	conf->keep_alive_period = CUSTOM_KEEP_ALIVE_SETTING;
#else
	conf->keep_alive_period = 28000;
#endif
	conf->force_wme_ac = 0;
	conf->stbc = -1;
	conf->phy_oclscdenable = -1;
#ifdef PKT_FILTER_SUPPORT
	memset(&conf->pkt_filter_add, 0, sizeof(conf_pkt_filter_add_t));
	memset(&conf->pkt_filter_del, 0, sizeof(conf_pkt_filter_del_t));
#endif
	conf->srl = -1;
	conf->lrl = -1;
	conf->bcn_timeout = 15;
	if (conf->chip == BCM4339_CHIP_ID) {
		conf->bus_txglom = 8;
		conf->ampdu_ba_wsize = 40;
	}
	conf->kso_enable = TRUE;
	conf->spect = -1;

	return 0;
}

int
dhd_conf_reset(dhd_pub_t *dhd)
{
	dhd_conf_free_mac_list(&dhd->conf->fw_by_mac);
	dhd_conf_free_mac_list(&dhd->conf->nv_by_mac);
	memset(dhd->conf, 0, sizeof(dhd_conf_t));
	return 0;
}

int
dhd_conf_attach(dhd_pub_t *dhd)
{
	dhd_conf_t *conf;

	CONFIG_TRACE(("%s: Enter\n", __FUNCTION__));

	if (dhd->conf != NULL) {
		printf("%s: config is attached before!\n", __FUNCTION__);
		return 0;
	}
	/* Allocate private bus interface state */
	if (!(conf = MALLOC(dhd->osh, sizeof(dhd_conf_t)))) {
		CONFIG_ERROR(("%s: MALLOC failed\n", __FUNCTION__));
		goto fail;
	}
	memset(conf, 0, sizeof(dhd_conf_t));

	dhd->conf = conf;

	return 0;

fail:
	if (conf != NULL)
		MFREE(dhd->osh, conf, sizeof(dhd_conf_t));
	return BCME_NOMEM;
}

void
dhd_conf_detach(dhd_pub_t *dhd)
{
	CONFIG_TRACE(("%s: Enter\n", __FUNCTION__));

	if (dhd->conf) {
		dhd_conf_free_mac_list(&dhd->conf->fw_by_mac);
		dhd_conf_free_mac_list(&dhd->conf->nv_by_mac);
		MFREE(dhd->osh, dhd->conf, sizeof(dhd_conf_t));
	}
	dhd->conf = NULL;
}
