/* SPDX-License-Identifier: GPL-2.0 */
#include <typedefs.h>
#include <osl.h>

#include <bcmutils.h>
#include <hndsoc.h>
#include <bcmsdbus.h>
#if defined(HW_OOB) || defined(FORCE_WOWLAN)
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

#define MAXSZ_BUF		1000
#define	MAXSZ_CONFIG	4096

#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i
#define htodchanspec(i) i
#define dtohchanspec(i) i

typedef struct cihp_name_map_t {
	uint chip;
	uint chiprev;
	uint ag_type;
	bool clm;
	char *chip_name;
	char *module_name;
} cihp_name_map_t;

/* Map of WLC_E events to connection failure strings */
#define DONT_CARE	9999
const cihp_name_map_t chip_name_map [] = {
	/* ChipID			Chiprev	AG	 	CLM		ChipName	ModuleName  */
#ifdef BCMSDIO
	{BCM43362_CHIP_ID,	0,	DONT_CARE,	FALSE,	"RK901a0",			""},
	//{BCM43362_CHIP_ID,	1,	DONT_CARE,	FALSE,	"RK901a2",			"AP6210"},
	{BCM43362_CHIP_ID,	1,	DONT_CARE,	FALSE,	"bcm40181a2",			"ap6181"},
	{BCM4330_CHIP_ID,	4,	FW_TYPE_G,	FALSE,	"RK903b2",			""},
	{BCM4330_CHIP_ID,	4,	FW_TYPE_AG,	FALSE,	"RK903_ag",			"AP6330"},
	{BCM43430_CHIP_ID,	0,	DONT_CARE,	FALSE,	"bcm43438a0",		"ap6212"},
	{BCM43430_CHIP_ID,	1,	DONT_CARE,	FALSE,	"bcm43438a1",		"ap6212a"},
	{BCM43430_CHIP_ID,	2,	DONT_CARE,	FALSE,	"bcm43436b0",		"ap6236"},
	{BCM43012_CHIP_ID,	1,	DONT_CARE,	TRUE,	"bcm43013b0",		""},
	{BCM4334_CHIP_ID,	3,	DONT_CARE,	FALSE,	"bcm4334b1_ag",		""},
	{BCM43340_CHIP_ID,	2,	DONT_CARE,	FALSE,	"bcm43341b0_ag",	""},
	{BCM43341_CHIP_ID,	2,	DONT_CARE,	FALSE,	"bcm43341b0_ag",	""},
	{BCM4324_CHIP_ID,	5,	DONT_CARE,	FALSE,	"bcm43241b4_ag",	"ap62x2"},
	{BCM4335_CHIP_ID,	2,	DONT_CARE,	FALSE,	"bcm4339a0_ag",		"AP6335"},
	{BCM4339_CHIP_ID,	1,	DONT_CARE,	FALSE,	"bcm4339a0_ag",		"AP6335"},
	{BCM4345_CHIP_ID,	6,	DONT_CARE,	FALSE,	"bcm43455c0_ag",	"ap6255"},
	{BCM43454_CHIP_ID,	6,	DONT_CARE,	FALSE,	"bcm43455c0_ag",	""},
	{BCM4345_CHIP_ID,	9,	DONT_CARE,	FALSE,	"bcm43456c5_ag",	"ap6256"},
	{BCM43454_CHIP_ID,	9,	DONT_CARE,	FALSE,	"bcm43456c5_ag",	""},
	{BCM4354_CHIP_ID,	1,	DONT_CARE,	FALSE,	"bcm4354a1_ag",		"ap6354"},
	{BCM4354_CHIP_ID,	2,	DONT_CARE,	FALSE,	"bcm4356a2_ag",		"ap6356"},
	{BCM4356_CHIP_ID,	2,	DONT_CARE,	FALSE,	"bcm4356a2_ag",		"ap6356"},
	{BCM4371_CHIP_ID,	2,	DONT_CARE,	FALSE,	"bcm4356a2_ag",		""},
	{BCM43569_CHIP_ID,	3,	DONT_CARE,	FALSE,	"bcm4358a3_ag",		""},
	{BCM4359_CHIP_ID,	5,	DONT_CARE,	FALSE,	"bcm4359b1_ag",		""},
	{BCM4359_CHIP_ID,	9,	DONT_CARE,	FALSE,	"bcm4359c0_ag",		"ap6398s"},
	{BCM4362_CHIP_ID,	0,	DONT_CARE,	TRUE,	"bcm43752a0_ag",	""},
#endif
#ifdef BCMPCIE
	{BCM4354_CHIP_ID,	2,	DONT_CARE,	FALSE,	"bcm4356a2_pcie_ag",	""},
	{BCM4356_CHIP_ID,	2,	DONT_CARE,	FALSE,	"bcm4356a2_pcie_ag",	""},
	{BCM4359_CHIP_ID,	9,	DONT_CARE,	FALSE,	"bcm4359c0_pcie_ag",	""},
	{BCM4362_CHIP_ID,	0,	DONT_CARE,	TRUE,	"bcm43752a0_pcie_ag",	""},
#endif
#ifdef BCMDBUS
	{BCM43143_CHIP_ID,	2,	DONT_CARE,	FALSE,	"bcm43143b0",		""},
	{BCM43242_CHIP_ID,	1,	DONT_CARE,	FALSE,	"bcm43242a1_ag",	""},
	{BCM43569_CHIP_ID,	2,	DONT_CARE,	FALSE,	"bcm4358u_ag",		""},
#endif
};

#ifdef BCMSDIO
void
dhd_conf_free_mac_list(wl_mac_list_ctrl_t *mac_list)
{
	int i;

	CONFIG_TRACE(("%s called\n", __FUNCTION__));
	if (mac_list->m_mac_list_head) {
		for (i=0; i<mac_list->count; i++) {
			if (mac_list->m_mac_list_head[i].mac) {
				CONFIG_TRACE(("%s Free mac %p\n", __FUNCTION__, mac_list->m_mac_list_head[i].mac));
				kfree(mac_list->m_mac_list_head[i].mac);
			}
		}
		CONFIG_TRACE(("%s Free m_mac_list_head %p\n", __FUNCTION__, mac_list->m_mac_list_head));
		kfree(mac_list->m_mac_list_head);
	}
	mac_list->count = 0;
}

void
dhd_conf_free_chip_nv_path_list(wl_chip_nv_path_list_ctrl_t *chip_nv_list)
{
	CONFIG_TRACE(("%s called\n", __FUNCTION__));

	if (chip_nv_list->m_chip_nv_path_head) {
		CONFIG_TRACE(("%s Free %p\n", __FUNCTION__, chip_nv_list->m_chip_nv_path_head));
		kfree(chip_nv_list->m_chip_nv_path_head);
	}
	chip_nv_list->count = 0;
}

#if defined(HW_OOB) || defined(FORCE_WOWLAN)
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

#define SBSDIO_CIS_SIZE_LIMIT		0x200
#define F0_BLOCK_SIZE 32
int
dhd_conf_set_blksize(bcmsdh_info_t *sdh)
{
	int err = 0;
	uint fn, numfn;
	int32 blksize = 0, cur_blksize = 0;
	uint8 cisd;

	numfn = bcmsdh_query_iofnum(sdh);

	for (fn = 0; fn <= numfn; fn++) {
		if (!fn)
			blksize = F0_BLOCK_SIZE;
		else {
			bcmsdh_cisaddr_read(sdh, fn, &cisd, 24);
			blksize = cisd;
			bcmsdh_cisaddr_read(sdh, fn, &cisd, 25);
			blksize |= cisd << 8;
		}
#ifdef CUSTOM_SDIO_F2_BLKSIZE
		if (fn == 2 && blksize > CUSTOM_SDIO_F2_BLKSIZE) {
			blksize = CUSTOM_SDIO_F2_BLKSIZE;
		}
#endif
		bcmsdh_iovar_op(sdh, "sd_blocksize", &fn, sizeof(int32),
			&cur_blksize, sizeof(int32), FALSE);
		if (cur_blksize != blksize) {
			printf("%s: fn=%d, blksize=%d, cur_blksize=%d\n", __FUNCTION__,
				fn, blksize, cur_blksize);
			blksize |= (fn<<16);
			if (bcmsdh_iovar_op(sdh, "sd_blocksize", NULL, 0, &blksize,
				sizeof(blksize), TRUE) != BCME_OK) {
				DHD_ERROR(("%s: fail on %s get\n", __FUNCTION__, "sd_blocksize"));
				err = -1;
			}
		}
	}

	return err;
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
			printk("%s: value:", __FUNCTION__);
			for (i=0; i<tpl_link-1; i++) {
				printk("%02x ", ptr[i+1]);
				if ((i+1) % 16 == 0)
					printk("\n");
			}
			printk("\n");
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
					memcpy(mac, ptr+3, 6);
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
	int fw_type, fw_type_new;
	char *name_ptr;

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
	while (i > 0) {
		if (fw_path[i] == '/') {
			i++;
			break;
		}
		i--;
	}
	name_ptr = &fw_path[i];

	if (strstr(name_ptr, "_apsta"))
		fw_type = FW_TYPE_APSTA;
	else if (strstr(name_ptr, "_p2p"))
		fw_type = FW_TYPE_P2P;
	else if (strstr(name_ptr, "_mesh"))
		fw_type = FW_TYPE_MESH;
	else if (strstr(name_ptr, "_es"))
		fw_type = FW_TYPE_ES;
	else if (strstr(name_ptr, "_mfg"))
		fw_type = FW_TYPE_MFG;
	else
		fw_type = FW_TYPE_STA;

	for (i=0; i<fw_num; i++) {
		mac_num = mac_list[i].count;
		mac_range = mac_list[i].mac;
		if (strstr(mac_list[i].name, "_apsta"))
			fw_type_new = FW_TYPE_APSTA;
		else if (strstr(mac_list[i].name, "_p2p"))
			fw_type_new = FW_TYPE_P2P;
		else if (strstr(mac_list[i].name, "_mesh"))
			fw_type_new = FW_TYPE_MESH;
		else if (strstr(mac_list[i].name, "_es"))
			fw_type_new = FW_TYPE_ES;
		else if (strstr(mac_list[i].name, "_mfg"))
			fw_type_new = FW_TYPE_MFG;
		else
			fw_type_new = FW_TYPE_STA;
		if (fw_type != fw_type_new) {
			printf("%s: fw_typ=%d != fw_type_new=%d\n", __FUNCTION__, fw_type, fw_type_new);
			continue;
		}
		for (j=0; j<mac_num; j++) {
			if (oui == mac_range[j].oui) {
				if (nic >= mac_range[j].nic_start && nic <= mac_range[j].nic_end) {
					strcpy(name_ptr, mac_list[i].name);
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
	while (i > 0) {
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
#endif

void
dhd_conf_free_country_list(conf_country_list_t *country_list)
{
	int i;

	CONFIG_TRACE(("%s called\n", __FUNCTION__));
	for (i=0; i<country_list->count; i++) {
		if (country_list->cspec[i]) {
			CONFIG_TRACE(("%s Free cspec %p\n", __FUNCTION__, country_list->cspec[i]));
			kfree(country_list->cspec[i]);
		}
	}
	country_list->count = 0;
}

void
dhd_conf_set_fw_name_by_chip(dhd_pub_t *dhd, char *fw_path)
{
	int fw_type, ag_type;
	uint chip, chiprev;
	int i;
	char *name_ptr;

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
	while (i > 0) {
		if (fw_path[i] == '/') {
			i++;
			break;
		}
		i--;
	}
	name_ptr = &fw_path[i];
#ifdef BAND_AG
	ag_type = FW_TYPE_AG;
#else
	ag_type = strstr(name_ptr, "_ag") ? FW_TYPE_AG : FW_TYPE_G;
#endif
	if (strstr(name_ptr, "_apsta"))
		fw_type = FW_TYPE_APSTA;
	else if (strstr(name_ptr, "_p2p"))
		fw_type = FW_TYPE_P2P;
	else if (strstr(name_ptr, "_mesh"))
		fw_type = FW_TYPE_MESH;
	else if (strstr(name_ptr, "_es"))
		fw_type = FW_TYPE_ES;
	else if (strstr(name_ptr, "_mfg"))
		fw_type = FW_TYPE_MFG;
	else
		fw_type = FW_TYPE_STA;

	for (i = 0;  i < sizeof(chip_name_map)/sizeof(chip_name_map[0]);  i++) {
		const cihp_name_map_t* row = &chip_name_map[i];
		if (row->chip == chip && row->chiprev == chiprev &&
			(row->ag_type == ag_type || row->ag_type == DONT_CARE)) {
			strcpy(name_ptr, "fw_");
			strcat(fw_path, row->chip_name);
#ifdef BCMUSBDEV_COMPOSITE
			strcat(fw_path, "_cusb");
#endif
			if (fw_type == FW_TYPE_APSTA)
				strcat(fw_path, "_apsta.bin");
			else if (fw_type == FW_TYPE_P2P)
				strcat(fw_path, "_p2p.bin");
			else if (fw_type == FW_TYPE_MESH)
				strcat(fw_path, "_mesh.bin");
			else if (fw_type == FW_TYPE_ES)
				strcat(fw_path, "_es.bin");
			else if (fw_type == FW_TYPE_MFG)
				strcat(fw_path, "_mfg.bin");
			else
				strcat(fw_path, ".bin");
		}
	}

	dhd->conf->fw_type = fw_type;

	CONFIG_TRACE(("%s: firmware_path=%s\n", __FUNCTION__, fw_path));
}

void
dhd_conf_set_clm_name_by_chip(dhd_pub_t *dhd, char *clm_path)
{
	uint chip, chiprev;
	int i;
	char *name_ptr;

	chip = dhd->conf->chip;
	chiprev = dhd->conf->chiprev;

	if (clm_path[0] == '\0') {
		printf("clm path is null\n");
		return;
	}

	/* find out the last '/' */
	i = strlen(clm_path);
	while (i > 0) {
		if (clm_path[i] == '/') {
			i++;
			break;
		}
		i--;
	}
	name_ptr = &clm_path[i];

	for (i = 0;  i < sizeof(chip_name_map)/sizeof(chip_name_map[0]);  i++) {
		const cihp_name_map_t* row = &chip_name_map[i];
		if (row->chip == chip && row->chiprev == chiprev && row->clm) {
			strcpy(name_ptr, "clm_");
			strcat(clm_path, row->chip_name);
			strcat(clm_path, ".blob");
		}
	}

	CONFIG_TRACE(("%s: clm_path=%s\n", __FUNCTION__, clm_path));
}

void
dhd_conf_set_nv_name_by_chip(dhd_pub_t *dhd, char *nv_path)
{
	uint chip, chiprev;
	int i;
	char *name_ptr;

	chip = dhd->conf->chip;
	chiprev = dhd->conf->chiprev;

	if (nv_path[0] == '\0') {
#ifdef CONFIG_BCMDHD_NVRAM_PATH
		bcm_strncpy_s(nv_path, MOD_PARAM_PATHLEN-1, CONFIG_BCMDHD_NVRAM_PATH, MOD_PARAM_PATHLEN-1);
		if (nv_path[0] == '\0')
#endif
		{
			printf("nvram path is null\n");
			return;
		}
	}

	/* find out the last '/' */
	i = strlen(nv_path);
	while (i > 0) {
		if (nv_path[i] == '/') {
			i++;
			break;
		}
		i--;
	}
	name_ptr = &nv_path[i];

	for (i = 0;  i < sizeof(chip_name_map)/sizeof(chip_name_map[0]);  i++) {
		const cihp_name_map_t* row = &chip_name_map[i];
		if (row->chip == chip && row->chiprev == chiprev && strlen(row->module_name)) {
			strcpy(name_ptr, "nvram_");
			strcat(name_ptr, row->module_name);
#ifdef BCMUSBDEV_COMPOSITE
			strcat(name_ptr, "_cusb");
#endif
			strcat(name_ptr, ".txt");
		}
	}

	for (i=0; i<dhd->conf->nv_by_chip.count; i++) {
		if (chip==dhd->conf->nv_by_chip.m_chip_nv_path_head[i].chip &&
				chiprev==dhd->conf->nv_by_chip.m_chip_nv_path_head[i].chiprev) {
			strcpy(name_ptr, dhd->conf->nv_by_chip.m_chip_nv_path_head[i].name);
			break;
		}
	}

	CONFIG_TRACE(("%s: nvram_path=%s\n", __FUNCTION__, nv_path));
}

void
dhd_conf_set_path(dhd_pub_t *dhd, char *dst_name, char *dst_path, char *src_path)
{
	int i;

	if (src_path[0] == '\0') {
		printf("src_path is null\n");
		return;
	} else
		strcpy(dst_path, src_path);

	/* find out the last '/' */
	i = strlen(dst_path);
	while (i > 0) {
		if (dst_path[i] == '/') {
			i++;
			break;
		}
		i--;
	}
	strcpy(&dst_path[i], dst_name);

	CONFIG_TRACE(("%s: dst_path=%s\n", __FUNCTION__, dst_path));
}

#ifdef CONFIG_PATH_AUTO_SELECT
void
dhd_conf_set_conf_name_by_chip(dhd_pub_t *dhd, char *conf_path)
{
	uint chip, chiprev;
	int i;
	char *name_ptr;

	chip = dhd->conf->chip;
	chiprev = dhd->conf->chiprev;

	if (conf_path[0] == '\0') {
		printf("config path is null\n");
		return;
	}

	/* find out the last '/' */
	i = strlen(conf_path);
	while (i > 0) {
		if (conf_path[i] == '/') {
			i++;
			break;
		}
		i--;
	}
	name_ptr = &conf_path[i];

	for (i = 0;  i < sizeof(chip_name_map)/sizeof(chip_name_map[0]);  i++) {
		const cihp_name_map_t* row = &chip_name_map[i];
		if (row->chip == chip && row->chiprev == chiprev) {
			strcpy(name_ptr, "config_");
			strcat(conf_path, row->chip_name);
			strcat(conf_path, ".txt");
		}
	}

	CONFIG_TRACE(("%s: config_path=%s\n", __FUNCTION__, conf_path));
}
#endif

int
dhd_conf_set_intiovar(dhd_pub_t *dhd, uint cmd, char *name, int val,
	int def, bool down)
{
	int ret = -1;
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" + '\0' + bitvec  */

	if (val >= def) {
		if (down) {
			if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0)) < 0)
				CONFIG_ERROR(("%s: WLC_DOWN setting failed %d\n", __FUNCTION__, ret));
		}
		if (cmd == WLC_SET_VAR) {
			CONFIG_TRACE(("%s: set %s %d\n", __FUNCTION__, name, val));
			bcm_mkiovar(name, (char *)&val, sizeof(val), iovbuf, sizeof(iovbuf));
			if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
				CONFIG_ERROR(("%s: %s setting failed %d\n", __FUNCTION__, name, ret));
		} else {
			CONFIG_TRACE(("%s: set %s %d %d\n", __FUNCTION__, name, cmd, val));
			if ((ret = dhd_wl_ioctl_cmd(dhd, cmd, &val, sizeof(val), TRUE, 0)) < 0)
				CONFIG_ERROR(("%s: %s setting failed %d\n", __FUNCTION__, name, ret));
		}
	}

	return ret;
}

int
dhd_conf_set_bufiovar(dhd_pub_t *dhd, uint cmd, char *name, char *buf,
	int len, bool down)
{
	char iovbuf[WLC_IOCTL_SMLEN];
	int ret = -1;

	if (down) {
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0)) < 0)
			CONFIG_ERROR(("%s: WLC_DOWN setting failed %d\n", __FUNCTION__, ret));
	}

	if (cmd == WLC_SET_VAR) {
		bcm_mkiovar(name, buf, len, iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, cmd, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
			CONFIG_ERROR(("%s: %s setting failed %d\n", __FUNCTION__, name, ret));
	} else {
		if ((ret = dhd_wl_ioctl_cmd(dhd, cmd, buf, len, TRUE, 0)) < 0)
			CONFIG_ERROR(("%s: %s setting failed %d\n", __FUNCTION__, name, ret));
	}

	return ret;
}

int
dhd_conf_get_iovar(dhd_pub_t *dhd, int cmd, char *name, char *buf, int len, int ifidx)
{
	char iovbuf[WLC_IOCTL_SMLEN];
	int ret = -1;

	if (cmd == WLC_GET_VAR) {
		if (bcm_mkiovar(name, NULL, 0, iovbuf, sizeof(iovbuf))) {
			ret = dhd_wl_ioctl_cmd(dhd, cmd, iovbuf, sizeof(iovbuf), FALSE, ifidx);
			if (!ret) {
				memcpy(buf, iovbuf, len);
			} else {
				CONFIG_ERROR(("%s: get iovar %s failed %d\n", __FUNCTION__, name, ret));
			}
		} else {
			CONFIG_ERROR(("%s: mkiovar %s failed\n", __FUNCTION__, name));
		}
	} else {
		ret = dhd_wl_ioctl_cmd(dhd, cmd, buf, len, FALSE, 0);
		if (ret < 0)
			CONFIG_ERROR(("%s: get iovar %s failed %d\n", __FUNCTION__, name, ret));
	}

	return ret;
}

uint
dhd_conf_get_band(dhd_pub_t *dhd)
{
	int band = -1;

	if (dhd && dhd->conf)
		band = dhd->conf->band;
	else
		CONFIG_ERROR(("%s: dhd or conf is NULL\n", __FUNCTION__));

	return band;
}

int
dhd_conf_get_country(dhd_pub_t *dhd, wl_country_t *cspec)
{
	int bcmerror = -1;

	memset(cspec, 0, sizeof(wl_country_t));
	bcm_mkiovar("country", NULL, 0, (char*)cspec, sizeof(wl_country_t));
	if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, cspec, sizeof(wl_country_t), FALSE, 0)) < 0)
		CONFIG_ERROR(("%s: country code getting failed %d\n", __FUNCTION__, bcmerror));

	return bcmerror;
}

int
dhd_conf_map_country_list(dhd_pub_t *dhd, wl_country_t *cspec)
{
	int bcmerror = -1, i;
	struct dhd_conf *conf = dhd->conf;
	conf_country_list_t *country_list = &conf->country_list;

	for (i = 0; i < country_list->count; i++) {
		if (!strncmp(cspec->country_abbrev, country_list->cspec[i]->country_abbrev, 2)) {
			memcpy(cspec->ccode, country_list->cspec[i]->ccode, WLC_CNTRY_BUF_SZ);
			cspec->rev = country_list->cspec[i]->rev;
			bcmerror = 0;
		}
	}

	if (!bcmerror)
		printf("%s: %s/%d\n", __FUNCTION__, cspec->ccode, cspec->rev);

	return bcmerror;
}

int
dhd_conf_set_country(dhd_pub_t *dhd, wl_country_t *cspec)
{
	int bcmerror = -1;

	memset(&dhd->dhd_cspec, 0, sizeof(wl_country_t));

	printf("%s: set country %s, revision %d\n", __FUNCTION__, cspec->ccode, cspec->rev);
	dhd_conf_set_bufiovar(dhd, WLC_SET_VAR, "country", (char *)cspec, sizeof(wl_country_t), FALSE);
	dhd_conf_get_country(dhd, cspec);
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
	wl_country_t cspec;

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
		dhd_conf_map_country_list(dhd, &dhd->conf->cspec);
		if ((bcmerror = dhd_conf_set_country(dhd, &dhd->conf->cspec)) < 0) {
			strcpy(cspec.country_abbrev, "US");
			cspec.rev = 0;
			strcpy(cspec.ccode, "US");
			dhd_conf_map_country_list(dhd, &cspec);
			dhd_conf_set_country(dhd, &cspec);
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
	struct dhd_conf *conf = dhd->conf;

	dhd_roam_disable = conf->roam_off;
	dhd_conf_set_intiovar(dhd, WLC_SET_VAR, "roam_off", dhd->conf->roam_off, 0, FALSE);

	if (!conf->roam_off || !conf->roam_off_suspend) {
		printf("%s: set roam_trigger %d\n", __FUNCTION__, conf->roam_trigger[0]);
		dhd_conf_set_bufiovar(dhd, WLC_SET_ROAM_TRIGGER, "WLC_SET_ROAM_TRIGGER",
				(char *)conf->roam_trigger, sizeof(conf->roam_trigger), FALSE);

		printf("%s: set roam_scan_period %d\n", __FUNCTION__, conf->roam_scan_period[0]);
		dhd_conf_set_bufiovar(dhd, WLC_SET_ROAM_SCAN_PERIOD, "WLC_SET_ROAM_SCAN_PERIOD",
				(char *)conf->roam_scan_period, sizeof(conf->roam_scan_period), FALSE);

		printf("%s: set roam_delta %d\n", __FUNCTION__, conf->roam_delta[0]);
		dhd_conf_set_bufiovar(dhd, WLC_SET_ROAM_DELTA, "WLC_SET_ROAM_DELTA",
				(char *)conf->roam_delta, sizeof(conf->roam_delta), FALSE);

		dhd_conf_set_intiovar(dhd, WLC_SET_VAR, "fullroamperiod", dhd->conf->fullroamperiod, 1, FALSE);
	}

	return bcmerror;
}

void
dhd_conf_set_bw_cap(dhd_pub_t *dhd)
{
	struct {
		u32 band;
		u32 bw_cap;
	} param = {0, 0};

	if (dhd->conf->bw_cap[0] >= 0) {
		memset(&param, 0, sizeof(param));
		param.band = WLC_BAND_2G;
		param.bw_cap = (uint)dhd->conf->bw_cap[0];
		printf("%s: set bw_cap 2g 0x%x\n", __FUNCTION__, param.bw_cap);
		dhd_conf_set_bufiovar(dhd, WLC_SET_VAR, "bw_cap", (char *)&param, sizeof(param), TRUE);
	}

	if (dhd->conf->bw_cap[1] >= 0) {
		memset(&param, 0, sizeof(param));
		param.band = WLC_BAND_5G;
		param.bw_cap = (uint)dhd->conf->bw_cap[1];
		printf("%s: set bw_cap 5g 0x%x\n", __FUNCTION__, param.bw_cap);
		dhd_conf_set_bufiovar(dhd, WLC_SET_VAR, "bw_cap", (char *)&param, sizeof(param), TRUE);
	}
}

void
dhd_conf_get_wme(dhd_pub_t *dhd, int mode, edcf_acparam_t *acp)
{
	int bcmerror = -1;
	char iovbuf[WLC_IOCTL_SMLEN];
	edcf_acparam_t *acparam;

	bzero(iovbuf, sizeof(iovbuf));

	/*
	 * Get current acparams, using buf as an input buffer.
	 * Return data is array of 4 ACs of wme params.
	 */
	if (mode == 0)
		bcm_mkiovar("wme_ac_sta", NULL, 0, iovbuf, sizeof(iovbuf));
	else
		bcm_mkiovar("wme_ac_ap", NULL, 0, iovbuf, sizeof(iovbuf));
	if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, iovbuf, sizeof(iovbuf), FALSE, 0)) < 0) {
		CONFIG_ERROR(("%s: wme_ac_sta getting failed %d\n", __FUNCTION__, bcmerror));
		return;
	}
	memcpy((char*)acp, iovbuf, sizeof(edcf_acparam_t)*AC_COUNT);

	acparam = &acp[AC_BK];
	CONFIG_TRACE(("%s: BK: aci %d aifsn %d ecwmin %d ecwmax %d txop 0x%x\n",
		__FUNCTION__,
		acparam->ACI, acparam->ACI&EDCF_AIFSN_MASK,
		acparam->ECW&EDCF_ECWMIN_MASK, (acparam->ECW&EDCF_ECWMAX_MASK)>>EDCF_ECWMAX_SHIFT,
		acparam->TXOP));
	acparam = &acp[AC_BE];
	CONFIG_TRACE(("%s: BE: aci %d aifsn %d ecwmin %d ecwmax %d txop 0x%x\n",
		__FUNCTION__,
		acparam->ACI, acparam->ACI&EDCF_AIFSN_MASK,
		acparam->ECW&EDCF_ECWMIN_MASK, (acparam->ECW&EDCF_ECWMAX_MASK)>>EDCF_ECWMAX_SHIFT,
		acparam->TXOP));
	acparam = &acp[AC_VI];
	CONFIG_TRACE(("%s: VI: aci %d aifsn %d ecwmin %d ecwmax %d txop 0x%x\n",
		__FUNCTION__,
		acparam->ACI, acparam->ACI&EDCF_AIFSN_MASK,
		acparam->ECW&EDCF_ECWMIN_MASK, (acparam->ECW&EDCF_ECWMAX_MASK)>>EDCF_ECWMAX_SHIFT,
		acparam->TXOP));
	acparam = &acp[AC_VO];
	CONFIG_TRACE(("%s: VO: aci %d aifsn %d ecwmin %d ecwmax %d txop 0x%x\n",
		__FUNCTION__,
		acparam->ACI, acparam->ACI&EDCF_AIFSN_MASK,
		acparam->ECW&EDCF_ECWMIN_MASK, (acparam->ECW&EDCF_ECWMAX_MASK)>>EDCF_ECWMAX_SHIFT,
		acparam->TXOP));

	return;
}

void
dhd_conf_update_wme(dhd_pub_t *dhd, int mode, edcf_acparam_t *acparam_cur, int aci)
{
	int aifsn, ecwmin, ecwmax, txop;
	edcf_acparam_t *acp;
	struct dhd_conf *conf = dhd->conf;
	wme_param_t *wme;

	if (mode == 0)
		wme = &conf->wme_sta;
	else
		wme = &conf->wme_ap;

	/* Default value */
	aifsn = acparam_cur->ACI&EDCF_AIFSN_MASK;
	ecwmin = acparam_cur->ECW&EDCF_ECWMIN_MASK;
	ecwmax = (acparam_cur->ECW&EDCF_ECWMAX_MASK)>>EDCF_ECWMAX_SHIFT;
	txop = acparam_cur->TXOP;

	/* Modified value */
	if (wme->aifsn[aci] > 0)
		aifsn = wme->aifsn[aci];
	if (wme->ecwmin[aci] > 0)
		ecwmin = wme->ecwmin[aci];
	if (wme->ecwmax[aci] > 0)
		ecwmax = wme->ecwmax[aci];
	if (wme->txop[aci] > 0)
		txop = wme->txop[aci];

	if (!(wme->aifsn[aci] || wme->ecwmin[aci] ||
			wme->ecwmax[aci] || wme->txop[aci]))
		return;

	/* Update */
	acp = acparam_cur;
	acp->ACI = (acp->ACI & ~EDCF_AIFSN_MASK) | (aifsn & EDCF_AIFSN_MASK);
	acp->ECW = ((ecwmax << EDCF_ECWMAX_SHIFT) & EDCF_ECWMAX_MASK) | (acp->ECW & EDCF_ECWMIN_MASK);
	acp->ECW = ((acp->ECW & EDCF_ECWMAX_MASK) | (ecwmin & EDCF_ECWMIN_MASK));
	acp->TXOP = txop;

	printf("%s: wme_ac %s aci %d aifsn %d ecwmin %d ecwmax %d txop 0x%x\n",
		__FUNCTION__, mode?"ap":"sta",
		acp->ACI, acp->ACI&EDCF_AIFSN_MASK,
		acp->ECW&EDCF_ECWMIN_MASK, (acp->ECW&EDCF_ECWMAX_MASK)>>EDCF_ECWMAX_SHIFT,
		acp->TXOP);

	/*
	* Now use buf as an output buffer.
	* Put WME acparams after "wme_ac\0" in buf.
	* NOTE: only one of the four ACs can be set at a time.
	*/
	if (mode == 0)
		dhd_conf_set_bufiovar(dhd, WLC_SET_VAR, "wme_ac_sta", (char *)acp, sizeof(edcf_acparam_t), FALSE);
	else
		dhd_conf_set_bufiovar(dhd, WLC_SET_VAR, "wme_ac_ap", (char *)acp, sizeof(edcf_acparam_t), FALSE);

}

void
dhd_conf_set_wme(dhd_pub_t *dhd, int mode)
{
	edcf_acparam_t acparam_cur[AC_COUNT];

	if (dhd && dhd->conf) {
		if (!dhd->conf->force_wme_ac) {
			CONFIG_TRACE(("%s: force_wme_ac is not enabled %d\n",
				__FUNCTION__, dhd->conf->force_wme_ac));
			return;
		}

		CONFIG_TRACE(("%s: Before change:\n", __FUNCTION__));
		dhd_conf_get_wme(dhd, mode, acparam_cur);

		dhd_conf_update_wme(dhd, mode, &acparam_cur[AC_BK], AC_BK);
		dhd_conf_update_wme(dhd, mode, &acparam_cur[AC_BE], AC_BE);
		dhd_conf_update_wme(dhd, mode, &acparam_cur[AC_VI], AC_VI);
		dhd_conf_update_wme(dhd, mode, &acparam_cur[AC_VO], AC_VO);

		CONFIG_TRACE(("%s: After change:\n", __FUNCTION__));
		dhd_conf_get_wme(dhd, mode, acparam_cur);
	} else {
		CONFIG_ERROR(("%s: dhd or conf is NULL\n", __FUNCTION__));
	}

	return;
}

void
dhd_conf_set_mchan_bw(dhd_pub_t *dhd, int p2p_mode, int miracast_mode)
{
	int i;
	struct dhd_conf *conf = dhd->conf;
	bool set = true;

	for (i=0; i<MCHAN_MAX_NUM; i++) {
		set = true;
		set &= (conf->mchan[i].bw >= 0);
		set &= ((conf->mchan[i].p2p_mode == -1) | (conf->mchan[i].p2p_mode == p2p_mode));
		set &= ((conf->mchan[i].miracast_mode == -1) | (conf->mchan[i].miracast_mode == miracast_mode));
		if (set) {
			dhd_conf_set_intiovar(dhd, WLC_SET_VAR, "mchan_bw", conf->mchan[i].bw, 0, FALSE);
		}
	}

	return;
}

#ifdef PKT_FILTER_SUPPORT
void
dhd_conf_add_pkt_filter(dhd_pub_t *dhd)
{
	int i, j;
	char str[16];
#define MACS "%02x%02x%02x%02x%02x%02x"

	/*
	 * Filter in less pkt: ARP(0x0806, ID is 105), BRCM(0x886C), 802.1X(0x888E)
	 *   1) dhd_master_mode=1
	 *   2) pkt_filter_del=100, 102, 103, 104, 105
	 *   3) pkt_filter_add=131 0 0 12 0xFFFF 0x886C, 132 0 0 12 0xFFFF 0x888E
	 *   4) magic_pkt_filter_add=141 0 1 12
	 */
	for(i=0; i<dhd->conf->pkt_filter_add.count; i++) {
		dhd->pktfilter[i+dhd->pktfilter_count] = dhd->conf->pkt_filter_add.filter[i];
		printf("%s: %s\n", __FUNCTION__, dhd->pktfilter[i+dhd->pktfilter_count]);
	}
	dhd->pktfilter_count += i;

	if (dhd->conf->magic_pkt_filter_add) {
		strcat(dhd->conf->magic_pkt_filter_add, " 0x");
		strcat(dhd->conf->magic_pkt_filter_add, "FFFFFFFFFFFF");
		for (j=0; j<16; j++)
			strcat(dhd->conf->magic_pkt_filter_add, "FFFFFFFFFFFF");
		strcat(dhd->conf->magic_pkt_filter_add, " 0x");
		strcat(dhd->conf->magic_pkt_filter_add, "FFFFFFFFFFFF");
		sprintf(str, MACS, MAC2STRDBG(dhd->mac.octet));
		for (j=0; j<16; j++)
			strncat(dhd->conf->magic_pkt_filter_add, str, 12);
		dhd->pktfilter[dhd->pktfilter_count] = dhd->conf->magic_pkt_filter_add;
		dhd->pktfilter_count += 1;
	}
}

bool
dhd_conf_del_pkt_filter(dhd_pub_t *dhd, uint32 id)
{
	int i;

	if (dhd && dhd->conf) {
		for (i=0; i<dhd->conf->pkt_filter_del.count; i++) {
			if (id == dhd->conf->pkt_filter_del.id[i]) {
				printf("%s: %d\n", __FUNCTION__, dhd->conf->pkt_filter_del.id[i]);
				return true;
			}
		}
		return false;
	}
	return false;
}

void
dhd_conf_discard_pkt_filter(dhd_pub_t *dhd)
{
	dhd->pktfilter_count = 6;
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
	dhd->pktfilter[dhd->pktfilter_count] = "121 0 0 12 0xFFFF000000000000000000FF000000000000000000000000FFFF 0x0800000000000000000000110000000000000000000000000089";
	dhd->pktfilter_count++;

}
#endif /* PKT_FILTER_SUPPORT */

int
dhd_conf_get_pm(dhd_pub_t *dhd)
{
	if (dhd && dhd->conf) {
		return dhd->conf->pm;
	}
	return -1;
}

#define AP_IN_SUSPEND 1
#define AP_DOWN_IN_SUSPEND 2
int
dhd_conf_get_ap_mode_in_suspend(dhd_pub_t *dhd)
{
	int mode = 0;

	/* returned ap_in_suspend value:
	 * 0: nothing
	 * 1: ap enabled in suspend
	 * 2: ap enabled, but down in suspend
	 */
	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
		mode = dhd->conf->ap_in_suspend;
	}

	return mode;
}

int
dhd_conf_set_ap_in_suspend(dhd_pub_t *dhd, int suspend)
{
	int mode = 0;
	uint wl_down = 1;

	mode = dhd_conf_get_ap_mode_in_suspend(dhd);
	if (mode)
		printf("%s: suspend %d, mode %d\n", __FUNCTION__, suspend, mode);
	if (suspend) {
		if (mode == AP_IN_SUSPEND) {
#ifdef SUSPEND_EVENT
			if (dhd->conf->suspend_eventmask_enable) {
				char *eventmask = dhd->conf->suspend_eventmask;
				dhd_conf_set_bufiovar(dhd, WLC_SET_VAR, "event_msgs", eventmask, sizeof(eventmask), TRUE);
			}
#endif
		} else if (mode == AP_DOWN_IN_SUSPEND)
			dhd_wl_ioctl_cmd(dhd, WLC_DOWN, (char *)&wl_down, sizeof(wl_down), TRUE, 0);
	} else {
		if (mode == AP_IN_SUSPEND) {
#ifdef SUSPEND_EVENT
			if (dhd->conf->suspend_eventmask_enable) {
				char *eventmask = dhd->conf->resume_eventmask;
				dhd_conf_set_bufiovar(dhd, WLC_SET_VAR, "event_msgs", eventmask, sizeof(eventmask), TRUE);
			}
#endif
		} else if (mode == AP_DOWN_IN_SUSPEND) {
			wl_down = 0;
			dhd_wl_ioctl_cmd(dhd, WLC_UP, (char *)&wl_down, sizeof(wl_down), TRUE, 0);
		}
	}

	return mode;
}

#ifdef PROP_TXSTATUS
int
dhd_conf_get_disable_proptx(dhd_pub_t *dhd)
{
	struct dhd_conf *conf = dhd->conf;
	int disable_proptx = -1;
	int fw_proptx = 0;

	/* check fw proptx priority:
	  * 1st: check fw support by wl cap
	  * 2nd: 4334/43340/43341/43241 support proptx but not show in wl cap, so enable it by default
	  * 	   if you would like to disable it, please set disable_proptx=1 in config.txt
	  * 3th: disable when proptxstatus not support in wl cap
	  */
	if (FW_SUPPORTED(dhd, proptxstatus)) {
		fw_proptx = 1;
	} else if (conf->chip == BCM4334_CHIP_ID || conf->chip == BCM43340_CHIP_ID ||
			dhd->conf->chip == BCM43340_CHIP_ID || conf->chip == BCM4324_CHIP_ID) {
		fw_proptx = 1;
	} else {
		fw_proptx = 0;
	}

	/* returned disable_proptx value:
	  * -1: disable in STA and enable in P2P(follow original dhd settings when PROP_TXSTATUS_VSDB enabled)
	  * 0: depend on fw support
	  * 1: always disable proptx
	  */
	if (conf->disable_proptx == 0) {
		// check fw support as well
		if (fw_proptx)
			disable_proptx = 0;
		else
			disable_proptx = 1;
	} else if (conf->disable_proptx >= 1) {
		disable_proptx = 1;
	} else {
		// check fw support as well
		if (fw_proptx)
			disable_proptx = -1;
		else
			disable_proptx = 1;
	}

	printf("%s: fw_proptx=%d, disable_proptx=%d\n", __FUNCTION__, fw_proptx, disable_proptx);

	return disable_proptx;
}
#endif

uint
pick_config_vars(char *varbuf, uint len, uint start_pos, char *pickbuf)
{
	bool findNewline, changenewline=FALSE, pick=FALSE;
	int column;
	uint n, pick_column=0;

	findNewline = FALSE;
	column = 0;

	if (start_pos >= len) {
		CONFIG_ERROR(("%s: wrong start pos\n", __FUNCTION__));
		return 0;
	}

	for (n = start_pos; n < len; n++) {
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

		if (column==0 && !pick) { // start to pick
			pick = TRUE;
			column++;
			pick_column = 0;
		} else {
			if (pick && column==0) { // stop to pick
				pick = FALSE;
				break;
			} else
				column++;
		}
		if (pick) {
			if (varbuf[n] == 0x9)
				continue;
			pickbuf[pick_column] = varbuf[n];
			pick_column++;
		}
	}

	return n; // return current position
}

bool
dhd_conf_read_log_level(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	char *data = full_param+len_param;

	if (!strncmp("dhd_msg_level=", full_param, len_param)) {
		dhd_msg_level = (int)simple_strtol(data, NULL, 0);
		printf("%s: dhd_msg_level = 0x%X\n", __FUNCTION__, dhd_msg_level);
	}
#ifdef BCMSDIO
	else if (!strncmp("sd_msglevel=", full_param, len_param)) {
		sd_msglevel = (int)simple_strtol(data, NULL, 0);
		printf("%s: sd_msglevel = 0x%X\n", __FUNCTION__, sd_msglevel);
	}
#endif
#ifdef BCMDBUS
	else if (!strncmp("dbus_msglevel=", full_param, len_param)) {
		dbus_msglevel = (int)simple_strtol(data, NULL, 0);
		printf("%s: dbus_msglevel = 0x%X\n", __FUNCTION__, dbus_msglevel);
	}
#endif
	else if (!strncmp("android_msg_level=", full_param, len_param)) {
		android_msg_level = (int)simple_strtol(data, NULL, 0);
		printf("%s: android_msg_level = 0x%X\n", __FUNCTION__, android_msg_level);
	}
	else if (!strncmp("config_msg_level=", full_param, len_param)) {
		config_msg_level = (int)simple_strtol(data, NULL, 0);
		printf("%s: config_msg_level = 0x%X\n", __FUNCTION__, config_msg_level);
	}
#ifdef WL_CFG80211
	else if (!strncmp("wl_dbg_level=", full_param, len_param)) {
		wl_dbg_level = (int)simple_strtol(data, NULL, 0);
		printf("%s: wl_dbg_level = 0x%X\n", __FUNCTION__, wl_dbg_level);
	}
#endif
#if defined(WL_WIRELESS_EXT)
	else if (!strncmp("iw_msg_level=", full_param, len_param)) {
		iw_msg_level = (int)simple_strtol(data, NULL, 0);
		printf("%s: iw_msg_level = 0x%X\n", __FUNCTION__, iw_msg_level);
	}
#endif
#if defined(DHD_DEBUG)
	else if (!strncmp("dhd_console_ms=", full_param, len_param)) {
		dhd_console_ms = (int)simple_strtol(data, NULL, 0);
		printf("%s: dhd_console_ms = 0x%X\n", __FUNCTION__, dhd_console_ms);
	}
#endif
	else
		return false;

	return true;
}

void
dhd_conf_read_wme_ac_value(wme_param_t *wme, char *pick, int ac_val)
{
	char *pick_tmp, *pch;

	pick_tmp = pick;
	pch = bcmstrstr(pick_tmp, "aifsn ");
	if (pch) {
		wme->aifsn[ac_val] = (int)simple_strtol(pch+strlen("aifsn "), NULL, 0);
		printf("%s: ac_val=%d, aifsn=%d\n", __FUNCTION__, ac_val, wme->aifsn[ac_val]);
	}
	pick_tmp = pick;
	pch = bcmstrstr(pick_tmp, "ecwmin ");
	if (pch) {
		wme->ecwmin[ac_val] = (int)simple_strtol(pch+strlen("ecwmin "), NULL, 0);
		printf("%s: ac_val=%d, ecwmin=%d\n", __FUNCTION__, ac_val, wme->ecwmin[ac_val]);
	}
	pick_tmp = pick;
	pch = bcmstrstr(pick_tmp, "ecwmax ");
	if (pch) {
		wme->ecwmax[ac_val] = (int)simple_strtol(pch+strlen("ecwmax "), NULL, 0);
		printf("%s: ac_val=%d, ecwmax=%d\n", __FUNCTION__, ac_val, wme->ecwmax[ac_val]);
	}
	pick_tmp = pick;
	pch = bcmstrstr(pick_tmp, "txop ");
	if (pch) {
		wme->txop[ac_val] = (int)simple_strtol(pch+strlen("txop "), NULL, 0);
		printf("%s: ac_val=%d, txop=0x%x\n", __FUNCTION__, ac_val, wme->txop[ac_val]);
	}

}

bool
dhd_conf_read_wme_ac_params(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	struct dhd_conf *conf = dhd->conf;
	char *data = full_param+len_param;

	// wme_ac_sta_be=aifsn 1 ecwmin 2 ecwmax 3 txop 0x5e
	// wme_ac_sta_vo=aifsn 1 ecwmin 1 ecwmax 1 txop 0x5e

	if (!strncmp("force_wme_ac=", full_param, len_param)) {
		conf->force_wme_ac = (int)simple_strtol(data, NULL, 10);
		printf("%s: force_wme_ac = %d\n", __FUNCTION__, conf->force_wme_ac);
	}
	else if (!strncmp("wme_ac_sta_be=", full_param, len_param)) {
		dhd_conf_read_wme_ac_value(&conf->wme_sta, data, AC_BE);
	}
	else if (!strncmp("wme_ac_sta_bk=", full_param, len_param)) {
		dhd_conf_read_wme_ac_value(&conf->wme_sta, data, AC_BK);
	}
	else if (!strncmp("wme_ac_sta_vi=", full_param, len_param)) {
		dhd_conf_read_wme_ac_value(&conf->wme_sta, data, AC_VI);
	}
	else if (!strncmp("wme_ac_sta_vo=", full_param, len_param)) {
		dhd_conf_read_wme_ac_value(&conf->wme_sta, data, AC_VO);
	}
	else if (!strncmp("wme_ac_ap_be=", full_param, len_param)) {
		dhd_conf_read_wme_ac_value(&conf->wme_ap, data, AC_BE);
	}
	else if (!strncmp("wme_ac_ap_bk=", full_param, len_param)) {
		dhd_conf_read_wme_ac_value(&conf->wme_ap, data, AC_BK);
	}
	else if (!strncmp("wme_ac_ap_vi=", full_param, len_param)) {
		dhd_conf_read_wme_ac_value(&conf->wme_ap, data, AC_VI);
	}
	else if (!strncmp("wme_ac_ap_vo=", full_param, len_param)) {
		dhd_conf_read_wme_ac_value(&conf->wme_ap, data, AC_VO);
	}
	else
		return false;

	return true;
}

bool
dhd_conf_read_fw_by_mac(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	int i, j;
	char *pch, *pick_tmp;
	wl_mac_list_t *mac_list;
	wl_mac_range_t *mac_range;
	struct dhd_conf *conf = dhd->conf;
	char *data = full_param+len_param;

	/* Process fw_by_mac:
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
	 */

	if (!strncmp("fw_by_mac=", full_param, len_param)) {
		pick_tmp = data;
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
	else
		return false;

	return true;
}

bool
dhd_conf_read_nv_by_mac(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	int i, j;
	char *pch, *pick_tmp;
	wl_mac_list_t *mac_list;
	wl_mac_range_t *mac_range;
	struct dhd_conf *conf = dhd->conf;
	char *data = full_param+len_param;

	/* Process nv_by_mac:
	 * [nv_by_mac]: The same format as fw_by_mac
	 */
	if (!strncmp("nv_by_mac=", full_param, len_param)) {
		pick_tmp = data;
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
	else
		return false;

	return true;
}

bool
dhd_conf_read_nv_by_chip(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	int i;
	char *pch, *pick_tmp;
	wl_chip_nv_path_t *chip_nv_path;
	struct dhd_conf *conf = dhd->conf;
	char *data = full_param+len_param;

	/* Process nv_by_chip:
	 * nv_by_chip=[nv_chip_num] \
	 *  [chip1] [chiprev1] [nv_name1] [chip2] [chiprev2] [nv_name2] \
	 * Ex: nv_by_chip=2 \
	 *  43430 0 nvram_ap6212.txt 43430 1 nvram_ap6212a.txt \
	 */
	if (!strncmp("nv_by_chip=", full_param, len_param)) {
		pick_tmp = data;
		pch = bcmstrtok(&pick_tmp, " ", 0);
		conf->nv_by_chip.count = (uint32)simple_strtol(pch, NULL, 0);
		if (!(chip_nv_path = kmalloc(sizeof(wl_mac_list_t)*conf->nv_by_chip.count, GFP_KERNEL))) {
			conf->nv_by_chip.count = 0;
			CONFIG_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
		}
		printf("%s: nv_by_chip_count=%d\n", __FUNCTION__, conf->nv_by_chip.count);
		conf->nv_by_chip.m_chip_nv_path_head = chip_nv_path;
		for (i=0; i<conf->nv_by_chip.count; i++) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			chip_nv_path[i].chip = (uint32)simple_strtol(pch, NULL, 0);
			pch = bcmstrtok(&pick_tmp, " ", 0);
			chip_nv_path[i].chiprev = (uint32)simple_strtol(pch, NULL, 0);
			pch = bcmstrtok(&pick_tmp, " ", 0);
			strcpy(chip_nv_path[i].name, pch);
			printf("%s: chip=0x%x, chiprev=%d, name=%s\n", __FUNCTION__,
				chip_nv_path[i].chip, chip_nv_path[i].chiprev, chip_nv_path[i].name);
		}
	}
	else
		return false;

	return true;
}

bool
dhd_conf_read_roam_params(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	struct dhd_conf *conf = dhd->conf;
	char *data = full_param+len_param;

	if (!strncmp("roam_off=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->roam_off = 0;
		else
			conf->roam_off = 1;
		printf("%s: roam_off = %d\n", __FUNCTION__, conf->roam_off);
	}
	else if (!strncmp("roam_off_suspend=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->roam_off_suspend = 0;
		else
			conf->roam_off_suspend = 1;
		printf("%s: roam_off_suspend = %d\n", __FUNCTION__, conf->roam_off_suspend);
	}
	else if (!strncmp("roam_trigger=", full_param, len_param)) {
		conf->roam_trigger[0] = (int)simple_strtol(data, NULL, 10);
		printf("%s: roam_trigger = %d\n", __FUNCTION__,
			conf->roam_trigger[0]);
	}
	else if (!strncmp("roam_scan_period=", full_param, len_param)) {
		conf->roam_scan_period[0] = (int)simple_strtol(data, NULL, 10);
		printf("%s: roam_scan_period = %d\n", __FUNCTION__,
			conf->roam_scan_period[0]);
	}
	else if (!strncmp("roam_delta=", full_param, len_param)) {
		conf->roam_delta[0] = (int)simple_strtol(data, NULL, 10);
		printf("%s: roam_delta = %d\n", __FUNCTION__, conf->roam_delta[0]);
	}
	else if (!strncmp("fullroamperiod=", full_param, len_param)) {
		conf->fullroamperiod = (int)simple_strtol(data, NULL, 10);
		printf("%s: fullroamperiod = %d\n", __FUNCTION__,
			conf->fullroamperiod);
	} else
		return false;

	return true;
}

bool
dhd_conf_read_country_list(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	int i;
	char *pch, *pick_tmp, *pick_tmp2;
	struct dhd_conf *conf = dhd->conf;
	char *data = full_param+len_param;
	wl_country_t *cspec;
	conf_country_list_t *country_list = NULL;

	/* Process country_list:
	 * country_list=[country1]:[ccode1]/[regrev1],
	 * [country2]:[ccode2]/[regrev2] \
	 * Ex: country_list=US:US/0, TW:TW/1
	 */
	if (!strncmp("country_list=", full_param, len_param)) {
		country_list = &dhd->conf->country_list;
	}
	if (country_list) {
		pick_tmp = data;
		for (i=0; i<CONFIG_COUNTRY_LIST_SIZE; i++) {
			pick_tmp2 = bcmstrtok(&pick_tmp, ", ", 0);
			if (!pick_tmp2)
				break;
			pch = bcmstrtok(&pick_tmp2, ":", 0);
			if (!pch)
				break;
			cspec = NULL;
			if (!(cspec = kmalloc(sizeof(wl_country_t), GFP_KERNEL))) {
				CONFIG_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
				break;
			}
			memset(cspec, 0, sizeof(wl_country_t));

			strcpy(cspec->country_abbrev, pch);
			pch = bcmstrtok(&pick_tmp2, "/", 0);
			if (!pch) {
				kfree(cspec);
				break;
			}
			memcpy(cspec->ccode, pch, 2);
			pch = bcmstrtok(&pick_tmp2, "/", 0);
			if (!pch) {
				kfree(cspec);
				break;
			}
			cspec->rev = (int32)simple_strtol(pch, NULL, 10);
			country_list->count++;
			country_list->cspec[i] = cspec;
			CONFIG_TRACE(("%s: country_list abbrev=%s, ccode=%s, regrev=%d\n", __FUNCTION__,
				cspec->country_abbrev, cspec->ccode, cspec->rev));
		}
		if (!strncmp("country_list=", full_param, len_param)) {
			printf("%s: %d country in list\n", __FUNCTION__, conf->country_list.count);
		}
	}
	else
		return false;

	return true;
}

bool
dhd_conf_read_mchan_params(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	int i;
	char *pch, *pick_tmp, *pick_tmp2;
	struct dhd_conf *conf = dhd->conf;
	char *data = full_param+len_param;

	/* Process mchan_bw:
	 * mchan_bw=[val]/[any/go/gc]/[any/source/sink]
	 * Ex: mchan_bw=80/go/source, 30/gc/sink
	 */
	if (!strncmp("mchan_bw=", full_param, len_param)) {
		pick_tmp = data;
		for (i=0; i<MCHAN_MAX_NUM; i++) {
			pick_tmp2 = bcmstrtok(&pick_tmp, ", ", 0);
			if (!pick_tmp2)
				break;
			pch = bcmstrtok(&pick_tmp2, "/", 0);
			if (!pch) {
				break;
			} else {
				conf->mchan[i].bw = (int)simple_strtol(pch, NULL, 0);
				if (conf->mchan[i].bw < 0 || conf->mchan[i].bw > 100) {
					CONFIG_ERROR(("%s: wrong bw %d\n", __FUNCTION__, conf->mchan[i].bw));
					conf->mchan[i].bw = 0;
					break;
				}
			}
			pch = bcmstrtok(&pick_tmp2, "/", 0);
			if (!pch) {
				break;
			} else {
				if (bcmstrstr(pch, "any")) {
					conf->mchan[i].p2p_mode = -1;
				} else if (bcmstrstr(pch, "go")) {
					conf->mchan[i].p2p_mode = WL_P2P_IF_GO;
				} else if (bcmstrstr(pch, "gc")) {
					conf->mchan[i].p2p_mode = WL_P2P_IF_CLIENT;
				}
			}
			pch = bcmstrtok(&pick_tmp2, "/", 0);
			if (!pch) {
				break;
			} else {
				if (bcmstrstr(pch, "any")) {
					conf->mchan[i].miracast_mode = -1;
				} else if (bcmstrstr(pch, "source")) {
					conf->mchan[i].miracast_mode = MIRACAST_SOURCE;
				} else if (bcmstrstr(pch, "sink")) {
					conf->mchan[i].miracast_mode = MIRACAST_SINK;
				}
			}
		}
		for (i=0; i<MCHAN_MAX_NUM; i++) {
			if (conf->mchan[i].bw >= 0)
				printf("%s: mchan_bw=%d/%d/%d\n", __FUNCTION__,
					conf->mchan[i].bw, conf->mchan[i].p2p_mode, conf->mchan[i].miracast_mode);
		}
	}
	else
		return false;

	return true;
}

#ifdef PKT_FILTER_SUPPORT
bool
dhd_conf_read_pkt_filter(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	struct dhd_conf *conf = dhd->conf;
	char *data = full_param+len_param;
	char *pch, *pick_tmp;
	int i;

	/* Process pkt filter:
	 * 1) pkt_filter_add=99 0 0 0 0x000000000000 0x000000000000
	 * 2) pkt_filter_del=100, 102, 103, 104, 105
	 * 3) magic_pkt_filter_add=141 0 1 12
	 */
	if (!strncmp("dhd_master_mode=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			dhd_master_mode = FALSE;
		else
			dhd_master_mode = TRUE;
		printf("%s: dhd_master_mode = %d\n", __FUNCTION__, dhd_master_mode);
	}
	else if (!strncmp("pkt_filter_add=", full_param, len_param)) {
		pick_tmp = data;
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
	else if (!strncmp("pkt_filter_del=", full_param, len_param)) {
		pick_tmp = data;
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
	else if (!strncmp("magic_pkt_filter_add=", full_param, len_param)) {
		if (!(conf->magic_pkt_filter_add = kmalloc(MAGIC_PKT_FILTER_LEN, GFP_KERNEL))) {
			CONFIG_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
		} else {
			memset(conf->magic_pkt_filter_add, 0, MAGIC_PKT_FILTER_LEN);
			strcpy(conf->magic_pkt_filter_add, data);
			printf("%s: magic_pkt_filter_add = %s\n", __FUNCTION__, conf->magic_pkt_filter_add);
		}
	}
	else
		return false;

	return true;
}
#endif

#ifdef ISAM_PREINIT
/*
 * isam_init=mode [sta|ap|apsta|dualap] vifname [wlan1]
 * isam_config=ifname [wlan0|wlan1] ssid [xxx] chan [x]
		 hidden [y|n] maxassoc [x]
		 amode [open|shared|wpapsk|wpa2psk|wpawpa2psk]
		 emode [none|wep|tkip|aes|tkipaes]
		 key [xxxxx]
 * isam_enable=ifname [wlan0|wlan1]
*/
bool
dhd_conf_read_isam(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	struct dhd_conf *conf = dhd->conf;
	char *data = full_param+len_param;

	if (!strncmp("isam_init=", full_param, len_param)) {
		sprintf(conf->isam_init, "isam_init %s", data);
		printf("%s: isam_init=%s\n", __FUNCTION__, conf->isam_init);
	}
	else if (!strncmp("isam_config=", full_param, len_param)) {
		sprintf(conf->isam_config, "isam_config %s", data);
		printf("%s: isam_config=%s\n", __FUNCTION__, conf->isam_config);
	}
	else if (!strncmp("isam_enable=", full_param, len_param)) {
		sprintf(conf->isam_enable, "isam_enable %s", data);
		printf("%s: isam_enable=%s\n", __FUNCTION__, conf->isam_enable);
	}
	else
		return false;

	return true;
}
#endif

#ifdef IDHCP
bool
dhd_conf_read_dhcp_params(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	struct dhd_conf *conf = dhd->conf;
	char *data = full_param+len_param;
	struct ipv4_addr ipa_set;

	if (!strncmp("dhcpc_enable=", full_param, len_param)) {
		conf->dhcpc_enable = (int)simple_strtol(data, NULL, 10);
		printf("%s: dhcpc_enable = %d\n", __FUNCTION__, conf->dhcpc_enable);
	}
	else if (!strncmp("dhcpd_enable=", full_param, len_param)) {
		conf->dhcpd_enable = (int)simple_strtol(data, NULL, 10);
		printf("%s: dhcpd_enable = %d\n", __FUNCTION__, conf->dhcpd_enable);
	}
	else if (!strncmp("dhcpd_ip_addr=", full_param, len_param)) {
		if (!bcm_atoipv4(data, &ipa_set))
			printf("%s : dhcpd_ip_addr adress setting failed.\n", __FUNCTION__);
		conf->dhcpd_ip_addr = ipa_set;
		printf("%s: dhcpd_ip_addr = %s\n",__FUNCTION__, data);
	}
	else if (!strncmp("dhcpd_ip_mask=", full_param, len_param)) {
		if (!bcm_atoipv4(data, &ipa_set))
			printf("%s : dhcpd_ip_mask adress setting failed.\n", __FUNCTION__);
		conf->dhcpd_ip_mask = ipa_set;
		printf("%s: dhcpd_ip_mask = %s\n",__FUNCTION__, data);
	}
	else if (!strncmp("dhcpd_ip_start=", full_param, len_param)) {
		if (!bcm_atoipv4(data, &ipa_set))
			printf("%s : dhcpd_ip_start adress setting failed.\n", __FUNCTION__);
		conf->dhcpd_ip_start = ipa_set;
		printf("%s: dhcpd_ip_start = %s\n",__FUNCTION__, data);
	}
	else if (!strncmp("dhcpd_ip_end=", full_param, len_param)) {
		if (!bcm_atoipv4(data, &ipa_set))
			printf("%s : dhcpd_ip_end adress setting failed.\n", __FUNCTION__);
		conf->dhcpd_ip_end = ipa_set;
		printf("%s: dhcpd_ip_end = %s\n",__FUNCTION__, data);
	}
	else
		return false;

	return true;
}
#endif

#ifdef BCMSDIO
bool
dhd_conf_read_sdio_params(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	struct dhd_conf *conf = dhd->conf;
	char *data = full_param+len_param;

	if (!strncmp("dhd_doflow=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			dhd_doflow = FALSE;
		else
			dhd_doflow = TRUE;
		printf("%s: dhd_doflow = %d\n", __FUNCTION__, dhd_doflow);
	}
	else if (!strncmp("dhd_slpauto=", full_param, len_param) ||
			!strncmp("kso_enable=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			dhd_slpauto = FALSE;
		else
			dhd_slpauto = TRUE;
		printf("%s: dhd_slpauto = %d\n", __FUNCTION__, dhd_slpauto);
	}
	else if (!strncmp("use_rxchain=", full_param, len_param)) {
		conf->use_rxchain = (int)simple_strtol(data, NULL, 10);
		printf("%s: use_rxchain = %d\n", __FUNCTION__, conf->use_rxchain);
	}
	else if (!strncmp("dhd_txminmax=", full_param, len_param)) {
		conf->dhd_txminmax = (uint)simple_strtol(data, NULL, 10);
		printf("%s: dhd_txminmax = %d\n", __FUNCTION__, conf->dhd_txminmax);
	}
	else if (!strncmp("txinrx_thres=", full_param, len_param)) {
		conf->txinrx_thres = (int)simple_strtol(data, NULL, 10);
		printf("%s: txinrx_thres = %d\n", __FUNCTION__, conf->txinrx_thres);
	}
	else if (!strncmp("sd_f2_blocksize=", full_param, len_param)) {
		conf->sd_f2_blocksize = (int)simple_strtol(data, NULL, 10);
		printf("%s: sd_f2_blocksize = %d\n", __FUNCTION__, conf->sd_f2_blocksize);
	}
#if defined(HW_OOB)
	else if (!strncmp("oob_enabled_later=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->oob_enabled_later = FALSE;
		else
			conf->oob_enabled_later = TRUE;
		printf("%s: oob_enabled_later = %d\n", __FUNCTION__, conf->oob_enabled_later);
	}
#endif
	else if (!strncmp("dpc_cpucore=", full_param, len_param)) {
		conf->dpc_cpucore = (int)simple_strtol(data, NULL, 10);
		printf("%s: dpc_cpucore = %d\n", __FUNCTION__, conf->dpc_cpucore);
	}
	else if (!strncmp("rxf_cpucore=", full_param, len_param)) {
		conf->rxf_cpucore = (int)simple_strtol(data, NULL, 10);
		printf("%s: rxf_cpucore = %d\n", __FUNCTION__, conf->rxf_cpucore);
	}
	else if (!strncmp("orphan_move=", full_param, len_param)) {
		conf->orphan_move = (int)simple_strtol(data, NULL, 10);
		printf("%s: orphan_move = %d\n", __FUNCTION__, conf->orphan_move);
	}
#if defined(BCMSDIOH_TXGLOM)
	else if (!strncmp("txglomsize=", full_param, len_param)) {
		conf->txglomsize = (uint)simple_strtol(data, NULL, 10);
		if (conf->txglomsize > SDPCM_MAXGLOM_SIZE)
			conf->txglomsize = SDPCM_MAXGLOM_SIZE;
		printf("%s: txglomsize = %d\n", __FUNCTION__, conf->txglomsize);
	}
	else if (!strncmp("txglom_ext=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->txglom_ext = FALSE;
		else
			conf->txglom_ext = TRUE;
		printf("%s: txglom_ext = %d\n", __FUNCTION__, conf->txglom_ext);
		if (conf->txglom_ext) {
			if ((conf->chip == BCM43362_CHIP_ID) || (conf->chip == BCM4330_CHIP_ID))
				conf->txglom_bucket_size = 1680;
			else if (conf->chip == BCM43340_CHIP_ID || conf->chip == BCM43341_CHIP_ID ||
					conf->chip == BCM4334_CHIP_ID || conf->chip == BCM4324_CHIP_ID)
				conf->txglom_bucket_size = 1684;
		}
		printf("%s: txglom_bucket_size = %d\n", __FUNCTION__, conf->txglom_bucket_size);
	}
	else if (!strncmp("bus:rxglom=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->bus_rxglom = FALSE;
		else
			conf->bus_rxglom = TRUE;
		printf("%s: bus:rxglom = %d\n", __FUNCTION__, conf->bus_rxglom);
	}
	else if (!strncmp("deferred_tx_len=", full_param, len_param)) {
		conf->deferred_tx_len = (int)simple_strtol(data, NULL, 10);
		printf("%s: deferred_tx_len = %d\n", __FUNCTION__, conf->deferred_tx_len);
	}
	else if (!strncmp("txctl_tmo_fix=", full_param, len_param)) {
		conf->txctl_tmo_fix = (int)simple_strtol(data, NULL, 0);
		printf("%s: txctl_tmo_fix = %d\n", __FUNCTION__, conf->txctl_tmo_fix);
	}
	else if (!strncmp("tx_max_offset=", full_param, len_param)) {
		conf->tx_max_offset = (int)simple_strtol(data, NULL, 10);
		printf("%s: tx_max_offset = %d\n", __FUNCTION__, conf->tx_max_offset);
	}
	else if (!strncmp("txglom_mode=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->txglom_mode = FALSE;
		else
			conf->txglom_mode = TRUE;
		printf("%s: txglom_mode = %d\n", __FUNCTION__, conf->txglom_mode);
	}
#endif
	else
		return false;

	return true;
}
#endif

#ifdef BCMPCIE
bool
dhd_conf_read_pcie_params(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	struct dhd_conf *conf = dhd->conf;
	char *data = full_param+len_param;

	if (!strncmp("bus:deepsleep_disable=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->bus_deepsleep_disable = 0;
		else
			conf->bus_deepsleep_disable = 1;
		printf("%s: bus:deepsleep_disable = %d\n", __FUNCTION__, conf->bus_deepsleep_disable);
	}
	else
		return false;

	return true;
}
#endif

bool
dhd_conf_read_pm_params(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	struct dhd_conf *conf = dhd->conf;
	char *data = full_param+len_param;

	if (!strncmp("deepsleep=", full_param, len_param)) {
		if (!strncmp(data, "1", 1))
			conf->deepsleep = TRUE;
		else
			conf->deepsleep = FALSE;
		printf("%s: deepsleep = %d\n", __FUNCTION__, conf->deepsleep);
	}
	else if (!strncmp("PM=", full_param, len_param)) {
		conf->pm = (int)simple_strtol(data, NULL, 10);
		printf("%s: PM = %d\n", __FUNCTION__, conf->pm);
	}
	else if (!strncmp("pm_in_suspend=", full_param, len_param)) {
		conf->pm_in_suspend = (int)simple_strtol(data, NULL, 10);
		printf("%s: pm_in_suspend = %d\n", __FUNCTION__, conf->pm_in_suspend);
	}
	else if (!strncmp("suspend_bcn_li_dtim=", full_param, len_param)) {
		conf->suspend_bcn_li_dtim = (int)simple_strtol(data, NULL, 10);
		printf("%s: suspend_bcn_li_dtim = %d\n", __FUNCTION__, conf->suspend_bcn_li_dtim);
	}
	else if (!strncmp("xmit_in_suspend=", full_param, len_param)) {
		if (!strncmp(data, "1", 1))
			conf->xmit_in_suspend = TRUE;
		else
			conf->xmit_in_suspend = FALSE;
		printf("%s: xmit_in_suspend = %d\n", __FUNCTION__, conf->xmit_in_suspend);
	}
	else if (!strncmp("ap_in_suspend=", full_param, len_param)) {
		conf->ap_in_suspend = (int)simple_strtol(data, NULL, 10);
		printf("%s: ap_in_suspend = %d\n", __FUNCTION__, conf->ap_in_suspend);
	}
#ifdef SUSPEND_EVENT
	else if (!strncmp("suspend_eventmask_enable=", full_param, len_param)) {
		if (!strncmp(data, "1", 1))
			conf->suspend_eventmask_enable = TRUE;
		else
			conf->suspend_eventmask_enable = FALSE;
		printf("%s: suspend_eventmask_enable = %d\n", __FUNCTION__, conf->suspend_eventmask_enable);
	}
#endif
	else
		return false;

	return true;
}

bool
dhd_conf_read_others(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	struct dhd_conf *conf = dhd->conf;
	char *data = full_param+len_param;
	uint len_data = strlen(data);
	char *pch, *pick_tmp;
	int i;

	if (!strncmp("dhd_poll=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->dhd_poll = 0;
		else
			conf->dhd_poll = 1;
		printf("%s: dhd_poll = %d\n", __FUNCTION__, conf->dhd_poll);
	}
	else if (!strncmp("dhd_watchdog_ms=", full_param, len_param)) {
		dhd_watchdog_ms = (int)simple_strtol(data, NULL, 10);
		printf("%s: dhd_watchdog_ms = %d\n", __FUNCTION__, dhd_watchdog_ms);
	}
	else if (!strncmp("band=", full_param, len_param)) {
		/* Process band:
		 * band=a for 5GHz only and band=b for 2.4GHz only
		 */
		if (!strcmp(data, "b"))
			conf->band = WLC_BAND_2G;
		else if (!strcmp(data, "a"))
			conf->band = WLC_BAND_5G;
		else
			conf->band = WLC_BAND_AUTO;
		printf("%s: band = %d\n", __FUNCTION__, conf->band);
	}
	else if (!strncmp("bw_cap_2g=", full_param, len_param)) {
		conf->bw_cap[0] = (uint)simple_strtol(data, NULL, 0);
		printf("%s: bw_cap_2g = %d\n", __FUNCTION__, conf->bw_cap[0]);
	}
	else if (!strncmp("bw_cap_5g=", full_param, len_param)) {
		conf->bw_cap[1] = (uint)simple_strtol(data, NULL, 0);
		printf("%s: bw_cap_5g = %d\n", __FUNCTION__, conf->bw_cap[1]);
	}
	else if (!strncmp("bw_cap=", full_param, len_param)) {
		pick_tmp = data;
		pch = bcmstrtok(&pick_tmp, " ,.-", 0);
		if (pch != NULL) {
			conf->bw_cap[0] = (uint32)simple_strtol(pch, NULL, 0);
			printf("%s: bw_cap 2g = %d\n", __FUNCTION__, conf->bw_cap[0]);
		}
		pch = bcmstrtok(&pick_tmp, " ,.-", 0);
		if (pch != NULL) {
			conf->bw_cap[1] = (uint32)simple_strtol(pch, NULL, 0);
			printf("%s: bw_cap 5g = %d\n", __FUNCTION__, conf->bw_cap[1]);
		}
	}
	else if (!strncmp("ccode=", full_param, len_param)) {
		memset(&conf->cspec, 0, sizeof(wl_country_t));
		memcpy(conf->cspec.country_abbrev, data, len_data);
		memcpy(conf->cspec.ccode, data, len_data);
		printf("%s: ccode = %s\n", __FUNCTION__, conf->cspec.ccode);
	}
	else if (!strncmp("regrev=", full_param, len_param)) {
		conf->cspec.rev = (int32)simple_strtol(data, NULL, 10);
		printf("%s: regrev = %d\n", __FUNCTION__, conf->cspec.rev);
	}
	else if (!strncmp("channels=", full_param, len_param)) {
		pick_tmp = data;
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
	else if (!strncmp("keep_alive_period=", full_param, len_param)) {
		conf->keep_alive_period = (uint)simple_strtol(data, NULL, 10);
		printf("%s: keep_alive_period = %d\n", __FUNCTION__,
			conf->keep_alive_period);
	}
	else if (!strncmp("phy_oclscdenable=", full_param, len_param)) {
		conf->phy_oclscdenable = (int)simple_strtol(data, NULL, 10);
		printf("%s: phy_oclscdenable = %d\n", __FUNCTION__, conf->phy_oclscdenable);
	}
	else if (!strncmp("srl=", full_param, len_param)) {
		conf->srl = (int)simple_strtol(data, NULL, 10);
		printf("%s: srl = %d\n", __FUNCTION__, conf->srl);
	}
	else if (!strncmp("lrl=", full_param, len_param)) {
		conf->lrl = (int)simple_strtol(data, NULL, 10);
		printf("%s: lrl = %d\n", __FUNCTION__, conf->lrl);
	}
	else if (!strncmp("bcn_timeout=", full_param, len_param)) {
		conf->bcn_timeout= (uint)simple_strtol(data, NULL, 10);
		printf("%s: bcn_timeout = %d\n", __FUNCTION__, conf->bcn_timeout);
	}
	else if (!strncmp("txbf=", full_param, len_param)) {
		conf->txbf = (int)simple_strtol(data, NULL, 10);
		printf("%s: txbf = %d\n", __FUNCTION__, conf->txbf);
	}
	else if (!strncmp("frameburst=", full_param, len_param)) {
		conf->frameburst = (int)simple_strtol(data, NULL, 10);
		printf("%s: frameburst = %d\n", __FUNCTION__, conf->frameburst);
	}
	else if (!strncmp("disable_proptx=", full_param, len_param)) {
		conf->disable_proptx = (int)simple_strtol(data, NULL, 10);
		printf("%s: disable_proptx = %d\n", __FUNCTION__, conf->disable_proptx);
	}
#ifdef DHDTCPACK_SUPPRESS
	else if (!strncmp("tcpack_sup_mode=", full_param, len_param)) {
		conf->tcpack_sup_mode = (uint)simple_strtol(data, NULL, 10);
		printf("%s: tcpack_sup_mode = %d\n", __FUNCTION__, conf->tcpack_sup_mode);
	}
#endif
	else if (!strncmp("pktprio8021x=", full_param, len_param)) {
		conf->pktprio8021x = (int)simple_strtol(data, NULL, 10);
		printf("%s: pktprio8021x = %d\n", __FUNCTION__, conf->pktprio8021x);
	}
#if defined(BCMSDIO) || defined(BCMPCIE)
	else if (!strncmp("dhd_txbound=", full_param, len_param)) {
		dhd_txbound = (uint)simple_strtol(data, NULL, 10);
		printf("%s: dhd_txbound = %d\n", __FUNCTION__, dhd_txbound);
	}
	else if (!strncmp("dhd_rxbound=", full_param, len_param)) {
		dhd_rxbound = (uint)simple_strtol(data, NULL, 10);
		printf("%s: dhd_rxbound = %d\n", __FUNCTION__, dhd_rxbound);
	}
#endif
	else if (!strncmp("tsq=", full_param, len_param)) {
		conf->tsq = (int)simple_strtol(data, NULL, 10);
		printf("%s: tsq = %d\n", __FUNCTION__, conf->tsq);
	}
	else if (!strncmp("ctrl_resched=", full_param, len_param)) {
		conf->ctrl_resched = (int)simple_strtol(data, NULL, 10);
		printf("%s: ctrl_resched = %d\n", __FUNCTION__, conf->ctrl_resched);
	}
	else if (!strncmp("dhd_ioctl_timeout_msec=", full_param, len_param)) {
		conf->dhd_ioctl_timeout_msec = (int)simple_strtol(data, NULL, 10);
		printf("%s: dhd_ioctl_timeout_msec = %d\n", __FUNCTION__, conf->dhd_ioctl_timeout_msec);
	}
	else if (!strncmp("in4way=", full_param, len_param)) {
		conf->in4way = (int)simple_strtol(data, NULL, 0);
		printf("%s: in4way = 0x%x\n", __FUNCTION__, conf->in4way);
	}
	else if (!strncmp("max_wait_gc_time=", full_param, len_param)) {
		conf->max_wait_gc_time = (int)simple_strtol(data, NULL, 0);
		printf("%s: max_wait_gc_time = %d\n", __FUNCTION__, conf->max_wait_gc_time);
	}
	else if (!strncmp("wl_preinit=", full_param, len_param)) {
		if (!(conf->wl_preinit = kmalloc(len_param+1, GFP_KERNEL))) {
			CONFIG_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
		} else {
			memset(conf->wl_preinit, 0, len_param+1);
			strcpy(conf->wl_preinit, data);
			printf("%s: wl_preinit = %s\n", __FUNCTION__, conf->wl_preinit);
		}
	}
	else
		return false;

	return true;
}

int
dhd_conf_read_config(dhd_pub_t *dhd, char *conf_path)
{
	int bcmerror = -1;
	uint len = 0, start_pos=0;
	void * image = NULL;
	char * memblock = NULL;
	char *bufp, *pick = NULL, *pch;
	bool conf_file_exists;
	uint len_param;

	conf_file_exists = ((conf_path != NULL) && (conf_path[0] != '\0'));
	if (!conf_file_exists) {
		printf("%s: config path %s\n", __FUNCTION__, conf_path);
		return (0);
	}

	if (conf_file_exists) {
		image = dhd_os_open_image(conf_path);
		if (image == NULL) {
			printf("%s: Ignore config file %s\n", __FUNCTION__, conf_path);
			goto err;
		}
	}

	memblock = MALLOC(dhd->osh, MAXSZ_CONFIG);
	if (memblock == NULL) {
		CONFIG_ERROR(("%s: Failed to allocate memory %d bytes\n",
			__FUNCTION__, MAXSZ_CONFIG));
		goto err;
	}

	pick = MALLOC(dhd->osh, MAXSZ_BUF);
	if (!pick) {
		CONFIG_ERROR(("%s: Failed to allocate memory %d bytes\n",
			__FUNCTION__, MAXSZ_BUF));
		goto err;
	}

	/* Read variables */
	if (conf_file_exists) {
		len = dhd_os_get_image_block(memblock, MAXSZ_CONFIG, image);
	}
	if (len > 0 && len < MAXSZ_CONFIG) {
		bufp = (char *)memblock;
		bufp[len] = 0;

		while (start_pos < len) {
			memset(pick, 0, MAXSZ_BUF);
			start_pos = pick_config_vars(bufp, len, start_pos, pick);
			pch = strchr(pick, '=');
			if (pch != NULL) {
				len_param = pch-pick+1;
				if (len_param == strlen(pick)) {
					CONFIG_ERROR(("%s: not a right parameter %s\n", __FUNCTION__, pick));
					continue;
				}
			} else {
				CONFIG_ERROR(("%s: not a right parameter %s\n", __FUNCTION__, pick));
				continue;
			}

			if (dhd_conf_read_log_level(dhd, pick, len_param))
				continue;
			else if (dhd_conf_read_roam_params(dhd, pick, len_param))
				continue;
			else if (dhd_conf_read_wme_ac_params(dhd, pick, len_param))
				continue;
			else if (dhd_conf_read_fw_by_mac(dhd, pick, len_param))
				continue;
			else if (dhd_conf_read_nv_by_mac(dhd, pick, len_param))
				continue;
			else if (dhd_conf_read_nv_by_chip(dhd, pick, len_param))
				continue;
			else if (dhd_conf_read_country_list(dhd, pick, len_param))
				continue;
			else if (dhd_conf_read_mchan_params(dhd, pick, len_param))
				continue;
#ifdef PKT_FILTER_SUPPORT
			else if (dhd_conf_read_pkt_filter(dhd, pick, len_param))
				continue;
#endif /* PKT_FILTER_SUPPORT */
#ifdef ISAM_PREINIT
			else if (dhd_conf_read_isam(dhd, pick, len_param))
				continue;
#endif /* ISAM_PREINIT */
#ifdef IDHCP
			else if (dhd_conf_read_dhcp_params(dhd, pick, len_param))
				continue;
#endif /* IDHCP */
#ifdef BCMSDIO
			else if (dhd_conf_read_sdio_params(dhd, pick, len_param))
				continue;
#endif /* BCMSDIO */
#ifdef BCMPCIE
			else if (dhd_conf_read_pcie_params(dhd, pick, len_param))
				continue;
#endif /* BCMPCIE */
			else if (dhd_conf_read_pm_params(dhd, pick, len_param))
				continue;
			else if (dhd_conf_read_others(dhd, pick, len_param))
				continue;
			else
				continue;
		}

		bcmerror = 0;
	} else {
		CONFIG_ERROR(("%s: error reading config file: %d\n", __FUNCTION__, len));
		bcmerror = BCME_SDIO_ERROR;
	}

err:
	if (pick)
		MFREE(dhd->osh, pick, MAXSZ_BUF);

	if (memblock)
		MFREE(dhd->osh, memblock, MAXSZ_CONFIG);

	if (image)
		dhd_os_close_image(image);

	return bcmerror;
}

int
dhd_conf_set_chiprev(dhd_pub_t *dhd, uint chip, uint chiprev)
{
	printf("%s: chip=0x%x, chiprev=%d\n", __FUNCTION__, chip, chiprev);
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

#ifdef BCMSDIO
void
dhd_conf_set_txglom_params(dhd_pub_t *dhd, bool enable)
{
	struct dhd_conf *conf = dhd->conf;

	if (enable) {
#if defined(BCMSDIOH_TXGLOM_EXT)
		if (conf->chip == BCM43362_CHIP_ID || conf->chip == BCM4330_CHIP_ID ||
				conf->chip == BCM43340_CHIP_ID || conf->chip == BCM43341_CHIP_ID ||
				conf->chip == BCM4334_CHIP_ID || conf->chip == BCM4324_CHIP_ID) {
			conf->txglom_mode = SDPCM_TXGLOM_CPY;
		}
#endif
		// other parameters set in preinit or config.txt
	} else {
		// clear txglom parameters
		conf->txglom_ext = FALSE;
		conf->txglom_bucket_size = 0;
		conf->txglomsize = 0;
		conf->deferred_tx_len = 0;
	}
	if (conf->txglom_ext)
		printf("%s: txglom_ext=%d, txglom_bucket_size=%d\n", __FUNCTION__,
			conf->txglom_ext, conf->txglom_bucket_size);
	printf("%s: txglom_mode=%s\n", __FUNCTION__,
 		conf->txglom_mode==SDPCM_TXGLOM_MDESC?"multi-desc":"copy");
	printf("%s: txglomsize=%d, deferred_tx_len=%d\n", __FUNCTION__,
		conf->txglomsize, conf->deferred_tx_len);
	printf("%s: txinrx_thres=%d, dhd_txminmax=%d\n", __FUNCTION__,
		conf->txinrx_thres, conf->dhd_txminmax);
	printf("%s: tx_max_offset=%d, txctl_tmo_fix=%d\n", __FUNCTION__,
		conf->tx_max_offset, conf->txctl_tmo_fix);

}
#endif

static int
dhd_conf_rsdb_mode(dhd_pub_t *dhd, char *buf)
{
	char *pch;
	wl_config_t rsdb_mode_cfg = {1, 0};

	pch = buf;
	rsdb_mode_cfg.config = (int)simple_strtol(pch, NULL, 0);

	if (pch) {
		dhd_conf_set_bufiovar(dhd, WLC_SET_VAR, "rsdb_mode", (char *)&rsdb_mode_cfg,
			sizeof(rsdb_mode_cfg), TRUE);
		printf("%s: rsdb_mode %d\n", __FUNCTION__, rsdb_mode_cfg.config);
	}

	return 0;
}

typedef int (tpl_parse_t)(dhd_pub_t *dhd, char *buf);

typedef struct iovar_tpl_t {
	int cmd;
	char *name;
	tpl_parse_t *parse;
} iovar_tpl_t;

const iovar_tpl_t iovar_tpl_list[] = {
	{WLC_SET_VAR,	"rsdb_mode",	dhd_conf_rsdb_mode},
};

static int iovar_tpl_parse(const iovar_tpl_t *tpl, int tpl_count,
	dhd_pub_t *dhd, int cmd, char *name, char *buf)
{
	int i, ret = 0;

	/* look for a matching code in the table */
	for (i = 0; i < tpl_count; i++, tpl++) {
		if (tpl->cmd == cmd && !strcmp(tpl->name, name))
			break;
	}
	if (i < tpl_count && tpl->parse) {
		ret = tpl->parse(dhd, buf);
	} else {
		ret = -1;
	}

	return ret;
}

bool
dhd_conf_set_wl_preinit(dhd_pub_t *dhd, char *data)
{
	int cmd, val, ret = 0;
	char name[32], *pch, *pick_tmp, *pick_tmp2;

	/* Process wl_preinit:
	 * wl_preinit=[cmd]=[val], [cmd]=[val]
	 * Ex: wl_preinit=86=0, mpc=0
	 */
	pick_tmp = data;
	while (pick_tmp && (pick_tmp2 = bcmstrtok(&pick_tmp, ",", 0)) != NULL) {
		pch = bcmstrtok(&pick_tmp2, "=", 0);
		if (!pch)
			break;
		if (*pch == ' ') {
			pch++;
		}
		memset(name, 0 , sizeof (name));
		cmd = (int)simple_strtol(pch, NULL, 0);
		if (cmd == 0) {
			cmd = WLC_SET_VAR;
			strcpy(name, pch);
		}
		pch = bcmstrtok(&pick_tmp2, ",", 0);
		if (!pch) {
			break;
		}
		ret = iovar_tpl_parse(iovar_tpl_list, ARRAY_SIZE(iovar_tpl_list),
			dhd, cmd, name, pch);
		if (ret) {
			val = (int)simple_strtol(pch, NULL, 0);
			dhd_conf_set_intiovar(dhd, cmd, name, val, -1, TRUE);
		}
	}

	return true;
}

void
dhd_conf_postinit_ioctls(dhd_pub_t *dhd)
{
	struct dhd_conf *conf = dhd->conf;

	dhd_conf_set_intiovar(dhd, WLC_UP, "up", 0, 0, FALSE);
	dhd_conf_map_country_list(dhd, &conf->cspec);
	dhd_conf_set_country(dhd, &conf->cspec);
	dhd_conf_fix_country(dhd);
	dhd_conf_get_country(dhd, &dhd->dhd_cspec);

	dhd_conf_set_intiovar(dhd, WLC_SET_BAND, "WLC_SET_BAND", conf->band, 0, FALSE);
	dhd_conf_set_intiovar(dhd, WLC_SET_VAR, "bcn_timeout", conf->bcn_timeout, 0, FALSE);
	dhd_conf_set_intiovar(dhd, WLC_SET_PM, "PM", conf->pm, 0, FALSE);
	dhd_conf_set_intiovar(dhd, WLC_SET_SRL, "WLC_SET_SRL", conf->srl, 0, TRUE);
	dhd_conf_set_intiovar(dhd, WLC_SET_LRL, "WLC_SET_LRL", conf->lrl, 0, FALSE);
	dhd_conf_set_bw_cap(dhd);
	dhd_conf_set_roam(dhd);

#if defined(BCMPCIE)
	dhd_conf_set_intiovar(dhd, WLC_SET_VAR, "bus:deepsleep_disable",
		conf->bus_deepsleep_disable, 0, FALSE);
#endif /* defined(BCMPCIE) */

#ifdef IDHCP
	dhd_conf_set_intiovar(dhd, WLC_SET_VAR, "dhcpc_enable", conf->dhcpc_enable, 0, FALSE);
	if (dhd->conf->dhcpd_enable >= 0) {
		dhd_conf_set_bufiovar(dhd, WLC_SET_VAR, "dhcpd_ip_addr",
			(char *)&conf->dhcpd_ip_addr, sizeof(conf->dhcpd_ip_addr), FALSE);
		dhd_conf_set_bufiovar(dhd, WLC_SET_VAR, "dhcpd_ip_mask",
			(char *)&conf->dhcpd_ip_mask, sizeof(conf->dhcpd_ip_mask), FALSE);
		dhd_conf_set_bufiovar(dhd, WLC_SET_VAR, "dhcpd_ip_start",
			(char *)&conf->dhcpd_ip_start, sizeof(conf->dhcpd_ip_start), FALSE);
		dhd_conf_set_bufiovar(dhd, WLC_SET_VAR, "dhcpd_ip_end",
			(char *)&conf->dhcpd_ip_end, sizeof(conf->dhcpd_ip_end), FALSE);
		dhd_conf_set_intiovar(dhd, WLC_SET_VAR, "dhcpd_enable",
			conf->dhcpd_enable, 0, FALSE);
	}
#endif
	dhd_conf_set_intiovar(dhd, WLC_SET_VAR, "txbf", conf->txbf, 0, FALSE);
	dhd_conf_set_intiovar(dhd, WLC_SET_FAKEFRAG, "WLC_SET_FAKEFRAG", conf->frameburst, 0, FALSE);

	dhd_conf_set_wl_preinit(dhd, conf->wl_preinit);

#ifndef WL_CFG80211
	dhd_conf_set_intiovar(dhd, WLC_UP, "up", 0, 0, FALSE);
#endif

}

int
dhd_conf_preinit(dhd_pub_t *dhd)
{
	struct dhd_conf *conf = dhd->conf;
	int i;

	CONFIG_TRACE(("%s: Enter\n", __FUNCTION__));

#ifdef BCMSDIO
	dhd_conf_free_mac_list(&conf->fw_by_mac);
	dhd_conf_free_mac_list(&conf->nv_by_mac);
	dhd_conf_free_chip_nv_path_list(&conf->nv_by_chip);
#endif
	dhd_conf_free_country_list(&conf->country_list);
	if (conf->magic_pkt_filter_add) {
		kfree(conf->magic_pkt_filter_add);
		conf->magic_pkt_filter_add = NULL;
	}
	if (conf->wl_preinit) {
		kfree(conf->wl_preinit);
		conf->wl_preinit = NULL;
	}
	memset(&conf->country_list, 0, sizeof(conf_country_list_t));
	conf->band = -1;
	memset(&conf->bw_cap, -1, sizeof(conf->bw_cap));
	if (conf->chip == BCM43362_CHIP_ID || conf->chip == BCM4330_CHIP_ID) {
		strcpy(conf->cspec.country_abbrev, "ALL");
		strcpy(conf->cspec.ccode, "ALL");
		conf->cspec.rev = 0;
	} else if (conf->chip == BCM4335_CHIP_ID || conf->chip == BCM4339_CHIP_ID ||
			conf->chip == BCM4354_CHIP_ID || conf->chip == BCM4356_CHIP_ID ||
			conf->chip == BCM4345_CHIP_ID || conf->chip == BCM4371_CHIP_ID ||
			conf->chip == BCM43569_CHIP_ID || conf->chip == BCM4359_CHIP_ID ||
			conf->chip == BCM4362_CHIP_ID) {
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
	memset(&conf->wme_sta, 0, sizeof(wme_param_t));
	memset(&conf->wme_ap, 0, sizeof(wme_param_t));
	conf->phy_oclscdenable = -1;
#ifdef PKT_FILTER_SUPPORT
	memset(&conf->pkt_filter_add, 0, sizeof(conf_pkt_filter_add_t));
	memset(&conf->pkt_filter_del, 0, sizeof(conf_pkt_filter_del_t));
#endif
	conf->srl = -1;
	conf->lrl = -1;
	conf->bcn_timeout = 16;
	conf->txbf = -1;
	conf->disable_proptx = -1;
	conf->dhd_poll = -1;
#ifdef BCMSDIO
	conf->use_rxchain = 0;
	conf->bus_rxglom = TRUE;
	conf->txglom_ext = FALSE;
	conf->tx_max_offset = 0;
	conf->txglomsize = SDPCM_DEFGLOM_SIZE;
	conf->txctl_tmo_fix = -1;
	conf->txglom_mode = SDPCM_TXGLOM_CPY;
	conf->deferred_tx_len = 0;
	conf->dhd_txminmax = 1;
	conf->txinrx_thres = -1;
	conf->sd_f2_blocksize = 0;
#if defined(HW_OOB)
	conf->oob_enabled_later = FALSE;
#endif
	conf->orphan_move = 0;
#endif
#ifdef BCMPCIE
	conf->bus_deepsleep_disable = 1;
#endif
	conf->dpc_cpucore = -1;
	conf->rxf_cpucore = -1;
	conf->frameburst = -1;
	conf->deepsleep = FALSE;
	conf->pm = -1;
	conf->pm_in_suspend = -1;
	conf->suspend_bcn_li_dtim = -1;
	conf->xmit_in_suspend = TRUE;
	conf->ap_in_suspend = 0;
#ifdef SUSPEND_EVENT
	conf->suspend_eventmask_enable = FALSE;
	memset(&conf->suspend_eventmask, 0, sizeof(conf->suspend_eventmask));
	memset(&conf->resume_eventmask, 0, sizeof(conf->resume_eventmask));
#endif
#ifdef IDHCP
	conf->dhcpc_enable = -1;
	conf->dhcpd_enable = -1;
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	conf->tsq = 10;
#else
	conf->tsq = 0;
#endif
#ifdef DHDTCPACK_SUPPRESS
#ifdef BCMPCIE
	conf->tcpack_sup_mode = TCPACK_SUP_DEFAULT;
#else
	conf->tcpack_sup_mode = TCPACK_SUP_OFF;
#endif
#endif
	conf->pktprio8021x = -1;
	conf->ctrl_resched = 2;
	conf->dhd_ioctl_timeout_msec = 0;
	conf->in4way = NO_SCAN_IN4WAY | WAIT_DISCONNECTED;
	conf->max_wait_gc_time = 300;
#ifdef ISAM_PREINIT
	memset(conf->isam_init, 0, sizeof(conf->isam_init));
	memset(conf->isam_config, 0, sizeof(conf->isam_config));
	memset(conf->isam_enable, 0, sizeof(conf->isam_enable));
#endif
	for (i=0; i<MCHAN_MAX_NUM; i++) {
		memset(&conf->mchan[i], -1, sizeof(mchan_params_t));
	}
	if (conf->chip == BCM4335_CHIP_ID || conf->chip == BCM4339_CHIP_ID ||
			conf->chip == BCM4354_CHIP_ID || conf->chip == BCM4356_CHIP_ID ||
			conf->chip == BCM4345_CHIP_ID || conf->chip == BCM4371_CHIP_ID ||
			conf->chip == BCM43569_CHIP_ID || conf->chip == BCM4359_CHIP_ID ||
			conf->chip == BCM4362_CHIP_ID) {
#ifdef DHDTCPACK_SUPPRESS
#ifdef BCMSDIO
		conf->tcpack_sup_mode = TCPACK_SUP_REPLACE;
#endif
#endif
#if defined(BCMSDIO) || defined(BCMPCIE)
		dhd_rxbound = 128;
		dhd_txbound = 64;
#endif
		conf->txbf = 1;
		conf->frameburst = 1;
#ifdef BCMSDIO
		conf->dhd_txminmax = -1;
		conf->txinrx_thres = 128;
		conf->sd_f2_blocksize = CUSTOM_SDIO_F2_BLKSIZE;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
		conf->orphan_move = 1;
#else
		conf->orphan_move = 0;
#endif
#endif
	}

#ifdef BCMSDIO
#if defined(BCMSDIOH_TXGLOM_EXT)
	if (conf->chip == BCM43362_CHIP_ID || conf->chip == BCM4330_CHIP_ID ||
			conf->chip == BCM43340_CHIP_ID || conf->chip == BCM43341_CHIP_ID ||
			conf->chip == BCM4334_CHIP_ID || conf->chip == BCM4324_CHIP_ID) {
		conf->txglom_ext = TRUE;
	} else {
		conf->txglom_ext = FALSE;
	}
	if (conf->chip == BCM43362_CHIP_ID || conf->chip == BCM4330_CHIP_ID) {
		conf->txglom_bucket_size = 1680; // fixed value, don't change
		conf->txglomsize = 6;
	}
	if (conf->chip == BCM4334_CHIP_ID || conf->chip == BCM43340_CHIP_ID ||
			conf->chip == BCM43341_CHIP_ID || conf->chip == BCM4324_CHIP_ID) {
		conf->txglom_bucket_size = 1684; // fixed value, don't change
		conf->txglomsize = 16;
	}
#endif
	if (conf->txglomsize > SDPCM_MAXGLOM_SIZE)
		conf->txglomsize = SDPCM_MAXGLOM_SIZE;
#endif

	return 0;
}

int
dhd_conf_reset(dhd_pub_t *dhd)
{
#ifdef BCMSDIO
	dhd_conf_free_mac_list(&dhd->conf->fw_by_mac);
	dhd_conf_free_mac_list(&dhd->conf->nv_by_mac);
	dhd_conf_free_chip_nv_path_list(&dhd->conf->nv_by_chip);
#endif
	dhd_conf_free_country_list(&dhd->conf->country_list);
	if (dhd->conf->magic_pkt_filter_add) {
		kfree(dhd->conf->magic_pkt_filter_add);
		dhd->conf->magic_pkt_filter_add = NULL;
	}
	if (dhd->conf->wl_preinit) {
		kfree(dhd->conf->wl_preinit);
		dhd->conf->wl_preinit = NULL;
	}
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
#ifdef BCMSDIO
		dhd_conf_free_mac_list(&dhd->conf->fw_by_mac);
		dhd_conf_free_mac_list(&dhd->conf->nv_by_mac);
		dhd_conf_free_chip_nv_path_list(&dhd->conf->nv_by_chip);
#endif
		dhd_conf_free_country_list(&dhd->conf->country_list);
		if (dhd->conf->magic_pkt_filter_add) {
			kfree(dhd->conf->magic_pkt_filter_add);
			dhd->conf->magic_pkt_filter_add = NULL;
		}
		if (dhd->conf->wl_preinit) {
			kfree(dhd->conf->wl_preinit);
			dhd->conf->wl_preinit = NULL;
		}
		MFREE(dhd->osh, dhd->conf, sizeof(dhd_conf_t));
	}
	dhd->conf = NULL;
}
