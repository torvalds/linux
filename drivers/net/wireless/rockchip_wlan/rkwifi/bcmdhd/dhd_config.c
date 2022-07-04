/* SPDX-License-Identifier: GPL-2.0 */
#include <typedefs.h>
#include <osl.h>

#include <bcmendian.h>
#include <bcmutils.h>
#include <hndsoc.h>
#include <bcmsdbus.h>
#if defined(HW_OOB) || defined(FORCE_WOWLAN)
#include <bcmdefs.h>
#include <bcmsdh.h>
#include <sdio.h>
#include <sbchipc.h>
#endif
#ifdef DHDTCPACK_SUPPRESS
#include <dhd_ip.h>
#endif /* DHDTCPACK_SUPPRESS */
#ifdef WL_CFG80211
#include <wl_cfg80211.h>
#endif

#include <dhd_config.h>
#include <dhd_dbg.h>
#include <wl_android.h>
#ifdef BCMPCIE
#include <dhd_flowring.h>
#endif

#if defined(BCMSDIO) || defined(BCMPCIE)
#include <dhd_linux.h>
#include <dhd_bus.h>
#ifdef BCMSDIO
#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#endif /* defined(BCMSDIO) */
#endif

/* message levels */
#define CONFIG_ERROR_LEVEL	(1 << 0)
#define CONFIG_TRACE_LEVEL	(1 << 1)
#define CONFIG_MSG_LEVEL	(1 << 0)

uint config_msg_level = CONFIG_ERROR_LEVEL | CONFIG_MSG_LEVEL;
uint dump_msg_level = 0;

#define CONFIG_MSG(x, args...) \
	do { \
		if (config_msg_level & CONFIG_MSG_LEVEL) { \
			printf("%s : " x, __func__, ## args); \
		} \
	} while (0)
#define CONFIG_ERROR(x, args...) \
	do { \
		if (config_msg_level & CONFIG_ERROR_LEVEL) { \
			printf("CONFIG-ERROR) %s : " x, __func__, ## args); \
		} \
	} while (0)
#define CONFIG_TRACE(x, args...) \
	do { \
		if (config_msg_level & CONFIG_TRACE_LEVEL) { \
			printf("CONFIG-TRACE) %s : " x, __func__, ## args); \
		} \
	} while (0)

#define MAXSZ_BUF		4096
#define MAXSZ_CONFIG	8192

#if defined(BCMSDIO) && defined(DYNAMIC_MAX_HDR_READ)
extern uint firstread;
#endif

#if defined(PROP_TXSTATUS)
#include <dhd_wlfc.h>
#endif /* PROP_TXSTATUS */

#define MAX_EVENT_BUF_NUM 16
typedef struct eventmsg_buf {
	u16 num;
	struct {
		u16 type;
		bool set;
	} event [MAX_EVENT_BUF_NUM];
} eventmsg_buf_t;

typedef struct chip_name_map_t {
	uint chip;
	uint chiprev;
	uint ag_type;
	char *chip_name;
	char *module_name;
} chip_name_map_t;

/* Map of WLC_E events to connection failure strings */
#define DONT_CARE	9999
const chip_name_map_t chip_name_map[] = {
	/* ChipID			Chiprev	AG	 	ChipName	ModuleName  */
#ifdef BCMSDIO
	{BCM43362_CHIP_ID,	0,	DONT_CARE,	"bcm40181a0",		""},
	{BCM43362_CHIP_ID,	1,	DONT_CARE,	"bcm40181a2",		"ap6181"},
	{BCM4330_CHIP_ID,	4,	FW_TYPE_G,	"RK903b2",			""},
	{BCM4330_CHIP_ID,	4,	FW_TYPE_AG,	"RK903_ag",			"AP6330"},
	{BCM43430_CHIP_ID,	0,	DONT_CARE,	"bcm43438a0",		"ap6212"},
	{BCM43430_CHIP_ID,	1,	DONT_CARE,	"bcm43438a1",		"ap6212a"},
	{BCM43430_CHIP_ID,	2,	DONT_CARE,	"bcm43436b0",		"ap6236"},
	{BCM43012_CHIP_ID,	1,	FW_TYPE_G,	"bcm43013b0",		""},
	{BCM43012_CHIP_ID,	1,	FW_TYPE_AG,	"bcm43013c0_ag",	""},
	{BCM43012_CHIP_ID,	2,	DONT_CARE,	"bcm43013c1_ag",	""},
	{BCM4334_CHIP_ID,	3,	DONT_CARE,	"bcm4334b1_ag",		""},
	{BCM43340_CHIP_ID,	2,	DONT_CARE,	"bcm43341b0_ag",	""},
	{BCM43341_CHIP_ID,	2,	DONT_CARE,	"bcm43341b0_ag",	""},
	{BCM4324_CHIP_ID,	5,	DONT_CARE,	"bcm43241b4_ag",	"ap62x2"},
	{BCM4335_CHIP_ID,	2,	DONT_CARE,	"bcm4339a0_ag",		"AP6335"},
	{BCM4339_CHIP_ID,	1,	DONT_CARE,	"bcm4339a0_ag",		"AP6335"},
	{BCM4345_CHIP_ID,	6,	DONT_CARE,	"bcm43455c0_ag",	"ap6255"},
	{BCM43454_CHIP_ID,	6,	DONT_CARE,	"bcm43455c0_ag",	""},
	{BCM4345_CHIP_ID,	9,	DONT_CARE,	"bcm43456c5_ag",	"ap6256"},
	{BCM43454_CHIP_ID,	9,	DONT_CARE,	"bcm43456c5_ag",	""},
	{BCM4354_CHIP_ID,	1,	DONT_CARE,	"bcm4354a1_ag",		"ap6354"},
	{BCM4354_CHIP_ID,	2,	DONT_CARE,	"bcm4356a2_ag",		"ap6356"},
	{BCM4356_CHIP_ID,	2,	DONT_CARE,	"bcm4356a2_ag",		"ap6356"},
	{BCM4371_CHIP_ID,	2,	DONT_CARE,	"bcm4356a2_ag",		""},
	{BCM43569_CHIP_ID,	3,	DONT_CARE,	"bcm4358a3_ag",		""},
	{BCM4359_CHIP_ID,	5,	DONT_CARE,	"bcm4359b1_ag",		""},
	{BCM4359_CHIP_ID,	9,	DONT_CARE,	"bcm4359c0_ag",		"ap6398s"},
	{BCM43751_CHIP_ID,	1,	DONT_CARE,	"bcm43751a1_ag",	""},
	{BCM43751_CHIP_ID,	2,	DONT_CARE,	"bcm43751a2_ag",	""},
	{BCM43752_CHIP_ID,	1,	DONT_CARE,	"bcm43752a1_ag",	""},
	{BCM43752_CHIP_ID,	2,	DONT_CARE,	"bcm43752a2_ag",	"ap6275s"},
#endif
#ifdef BCMPCIE
	{BCM4354_CHIP_ID,	2,	DONT_CARE,	"bcm4356a2_pcie_ag",	""},
	{BCM4356_CHIP_ID,	2,	DONT_CARE,	"bcm4356a2_pcie_ag",	""},
	{BCM4359_CHIP_ID,	9,	DONT_CARE,	"bcm4359c0_pcie_ag",	""},
	{BCM43751_CHIP_ID,	1,	DONT_CARE,	"bcm43751a1_pcie_ag",	""},
	{BCM43751_CHIP_ID,	2,	DONT_CARE,	"bcm43751a2_pcie_ag",	""},
	{BCM43752_CHIP_ID,	1,	DONT_CARE,	"bcm43752a1_pcie_ag",	""},
	{BCM43752_CHIP_ID,	2,	DONT_CARE,	"bcm43752a2_pcie_ag",	"AP6275P"},
	{BCM4375_CHIP_ID,	5,	DONT_CARE,	"bcm4375b4_pcie_ag",	""},
#endif
#ifdef BCMDBUS
	{BCM43143_CHIP_ID,	2,	DONT_CARE,	"bcm43143b0",		""},
	{BCM43242_CHIP_ID,	1,	DONT_CARE,	"bcm43242a1_ag",	""},
	{BCM43569_CHIP_ID,	2,	DONT_CARE,	"bcm4358u_ag",		""},
#endif
};

#ifdef UPDATE_MODULE_NAME
typedef void (compat_func_t)(dhd_pub_t *dhd);
typedef struct module_name_map_t {
	uint devid;
	uint chip;
	uint chiprev;
	uint svid;
	uint ssid;
	char *module_name;
	char *chip_name;
	compat_func_t *compat_func;
} module_name_map_t;

#if defined(BCMSDIO) || defined(BCMPCIE)
static void dhd_conf_compat_vht(dhd_pub_t *dhd);
#endif

const module_name_map_t module_name_map[] = {
	/* Devce ID			Chip ID			Chiprev	SVID	SSID
	 *  ModuleName		ChipName			Compat function
	 */
#ifdef BCMSDIO
	{BCM43751_CHIP_ID,	BCM43752_CHIP_ID,	2,	0, 0,
		"ap6398s2",		"bcm4359c51a2_ag",	dhd_conf_compat_vht},
	{BCM43751_CHIP_ID,	BCM43752_CHIP_ID,	2,	0, 0,
		"ap6398sr32",	"bcm4359c51a2_ag",	dhd_conf_compat_vht},
	{BCM43751_CHIP_ID,	BCM43752_CHIP_ID,	2,	0, 0,
		"ap6398sv",		"bcm4359c51a2_ag",	dhd_conf_compat_vht},
	{BCM43751_CHIP_ID,	BCM43752_CHIP_ID,	2,	0, 0,
		"ap6398sv3",	"bcm4359c51a2_ag",	dhd_conf_compat_vht},
#endif
#ifdef BCMPCIE
	{BCM43751_D11AX_ID,	BCM43752_CHIP_ID,	2,	0x179F, 0x003C,
		"ap6398p2",		"bcm4359c51a2_pcie_ag",	dhd_conf_compat_vht},
	{BCM43751_D11AX_ID,	BCM43752_CHIP_ID,	2,	0x17F9, 0x003C,
		"ap6398p2",		"bcm4359c51a2_pcie_ag",	dhd_conf_compat_vht},
	{BCM43751_D11AX_ID,	BCM43752_CHIP_ID,	2,	0x17F9, 0x003D,
		"ap6398pr32",	"bcm4359c51a2_pcie_ag",	dhd_conf_compat_vht},
	{BCM43751_D11AX_ID,	BCM43752_CHIP_ID,	2,	0x17F9, 0x003E,
		"ap6398pv",		"bcm4359c51a2_pcie_ag",	dhd_conf_compat_vht},
	{BCM43751_D11AX_ID,	BCM43752_CHIP_ID,	2,	0x17F9, 0x003F,
		"ap6398pv3",	"bcm4359c51a2_pcie_ag",	dhd_conf_compat_vht},
#endif
};
#endif

#ifdef BCMPCIE
typedef struct chip_cisaddr_map_t {
	uint chip;
	uint chiprev;
	uint start_addr;
	uint end_addr;
} chip_cisaddr_map_t;
const chip_cisaddr_map_t chip_cisaddr_map[] = {
	/* ChipID			Chiprev	Start	 	End  */
	{BCM4354_CHIP_ID,	2,		0x0,		0x0},
	{BCM4356_CHIP_ID,	2,		0x0,		0x0},
	{BCM4359_CHIP_ID,	9,		0x0,		0x0},
//	{BCM43752_CHIP_ID,	2,		0x18011120,	0x18011177},
//	{BCM4375_CHIP_ID,	5,		0x18011120,	0x18011177},
};
#endif

#ifdef DHD_TPUT_PATCH
extern int dhd_change_mtu(dhd_pub_t *dhd, int new_mtu, int ifidx);
#endif

void
dhd_conf_free_chip_nv_path_list(wl_chip_nv_path_list_ctrl_t *chip_nv_list)
{
	CONFIG_TRACE("called\n");

	if (chip_nv_list->m_chip_nv_path_head) {
		CONFIG_TRACE("Free %p\n", chip_nv_list->m_chip_nv_path_head);
		kfree(chip_nv_list->m_chip_nv_path_head);
		chip_nv_list->m_chip_nv_path_head = NULL;
	}
	chip_nv_list->count = 0;
}

#if defined(BCMSDIO) || defined(BCMPCIE)
typedef struct cis_tuple_format {
	uint8	id;
	uint8	len;	/* total length of tag and data */
	uint8	tag;
	uint8	data[1];
} cis_tuple_format_t;
#define SBSDIO_CIS_SIZE_LIMIT		0x200
#define SBSDIO_TUPLE_SIZE_LIMIT		0xff
#define CIS_TUPLE_ID_BRCM			0x80
#define CIS_TUPLE_TAG_MACADDR		0x19
#define CIS_TUPLE_ID_AMPAK			0x8e
#define CIS_TUPLE_TAG_MODULE		0x41
#define CIS_TUPLE_LENGTH		1
#define CIS_TUPLE_HDR_LEN		2
#endif

#ifdef BCMSDIO
#if defined(HW_OOB) || defined(FORCE_WOWLAN)
void
dhd_conf_set_hw_oob_intr(bcmsdh_info_t *sdh, struct si_pub *sih)
{
	uint32 gpiocontrol, addr;

	if (CHIPID(sih->chip) == BCM43362_CHIP_ID) {
		CONFIG_MSG("Enable HW OOB for 43362\n");
		addr = SI_ENUM_BASE(sih) + OFFSETOF(chipcregs_t, gpiocontrol);
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
dhd_conf_get_otp(dhd_pub_t *dhd, bcmsdh_info_t *sdh, si_t *sih)
{
	int i, err = -1;
	uint8 *cis, *ptr = 0;
	uint8 mac_header[3] = {0x80, 0x07, 0x19};
	cis_tuple_format_t *tuple;
	int totlen, len;

	if (!(cis = MALLOC(dhd->osh, SBSDIO_CIS_SIZE_LIMIT))) {
		CONFIG_ERROR("cis malloc failed\n");
	}
	bzero(cis, SBSDIO_CIS_SIZE_LIMIT);

	if ((err = bcmsdh_cis_read(sdh, 0, cis, SBSDIO_CIS_SIZE_LIMIT))) {
		CONFIG_ERROR("cis read err %d\n", err);
		MFREE(dhd->osh, cis, SBSDIO_CIS_SIZE_LIMIT);
		return;
	}
	tuple = (cis_tuple_format_t *)cis;
	totlen = SBSDIO_CIS_SIZE_LIMIT;
	if (config_msg_level & CONFIG_TRACE_LEVEL) {
		prhex("CIS", &tuple->id, totlen);
	}
	while (totlen >= (tuple->len + CIS_TUPLE_HDR_LEN)) {
		len = tuple->len;
		if ((config_msg_level & CONFIG_TRACE_LEVEL) && tuple->id) {
			prhex("TPL", &tuple->id, tuple->len + CIS_TUPLE_HDR_LEN);
		}
		if (tuple->id == 0xff || tuple->len == 0xff)
			break;
		if ((tuple->id == CIS_TUPLE_ID_BRCM) &&
				(tuple->tag == CIS_TUPLE_TAG_MACADDR) &&
				(totlen >= (int)(len + CIS_TUPLE_HDR_LEN))) {
			memcpy(&dhd->conf->otp_mac, tuple->data, ETHER_ADDR_LEN);
		}
#ifdef GET_OTP_MODULE_NAME
		else if (tuple->id == CIS_TUPLE_ID_AMPAK && (tuple->len) &&
				tuple->tag == CIS_TUPLE_TAG_MODULE) {
			int len = tuple->len - 1;
			if (len <= sizeof(dhd->conf->module_name) - 1) {
				strncpy(dhd->conf->module_name, tuple->data, len);
				CONFIG_MSG("module_name=%s\n", dhd->conf->module_name);
			} else {
				CONFIG_ERROR("len is too long %d >= %d\n",
					len, (int)sizeof(dhd->conf->module_name) - 1);
			}
		}
#endif
		tuple = (cis_tuple_format_t*)((uint8*)tuple + (len + CIS_TUPLE_HDR_LEN));
		totlen -= (len + CIS_TUPLE_HDR_LEN);
	}

	if (!memcmp(&ether_null, &dhd->conf->otp_mac, ETHER_ADDR_LEN)) {
		ptr = cis;
		/* Special OTP */
		if (bcmsdh_reg_read(sdh, SI_ENUM_BASE(sih), 4) == 0x16044330) {
			for (i=0; i<SBSDIO_CIS_SIZE_LIMIT; i++) {
				if (!memcmp(mac_header, ptr, 3)) {
					memcpy(&dhd->conf->otp_mac, ptr+3, ETHER_ADDR_LEN);
					break;
				}
				ptr++;
			}
		}
	}

	ASSERT(cis);
	MFREE(dhd->osh, cis, SBSDIO_CIS_SIZE_LIMIT);
}

#ifdef SET_FWNV_BY_MAC
void
dhd_conf_free_mac_list(wl_mac_list_ctrl_t *mac_list)
{
	int i;

	CONFIG_TRACE("called\n");
	if (mac_list->m_mac_list_head) {
		for (i=0; i<mac_list->count; i++) {
			if (mac_list->m_mac_list_head[i].mac) {
				CONFIG_TRACE("Free mac %p\n", mac_list->m_mac_list_head[i].mac);
				kfree(mac_list->m_mac_list_head[i].mac);
			}
		}
		CONFIG_TRACE("Free m_mac_list_head %p\n", mac_list->m_mac_list_head);
		kfree(mac_list->m_mac_list_head);
	}
	mac_list->count = 0;
}

void
dhd_conf_set_fw_name_by_mac(dhd_pub_t *dhd, char *fw_path)
{
	int i, j;
	uint8 *mac = (uint8 *)&dhd->conf->otp_mac;
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
	else if (strstr(name_ptr, "_ezmesh"))
		fw_type = FW_TYPE_EZMESH;
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
		else if (strstr(mac_list[i].name, "_ezmesh"))
			fw_type_new = FW_TYPE_EZMESH;
		else if (strstr(mac_list[i].name, "_es"))
			fw_type_new = FW_TYPE_ES;
		else if (strstr(mac_list[i].name, "_mfg"))
			fw_type_new = FW_TYPE_MFG;
		else
			fw_type_new = FW_TYPE_STA;
		if (fw_type != fw_type_new) {
			CONFIG_MSG("fw_typ=%d != fw_type_new=%d\n", fw_type, fw_type_new);
			continue;
		}
		for (j=0; j<mac_num; j++) {
			if (oui == mac_range[j].oui) {
				if (nic >= mac_range[j].nic_start && nic <= mac_range[j].nic_end) {
					strcpy(name_ptr, mac_list[i].name);
					CONFIG_MSG("matched oui=0x%06X, nic=0x%06X\n", oui, nic);
					CONFIG_MSG("fw_path=%s\n", fw_path);
					return;
				}
			}
		}
	}
}

void
dhd_conf_set_nv_name_by_mac(dhd_pub_t *dhd, char *nv_path)
{
	int i, j;
	uint8 *mac = (uint8 *)&dhd->conf->otp_mac;
	int nv_num=0, mac_num=0;
	uint32 oui, nic;
	wl_mac_list_t *mac_list;
	wl_mac_range_t *mac_range;
	char *pnv_name;

	mac_list = dhd->conf->nv_by_mac.m_mac_list_head;
	nv_num = dhd->conf->nv_by_mac.count;
	if (!mac_list || !nv_num)
		return;

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
					CONFIG_MSG("matched oui=0x%06X, nic=0x%06X\n", oui, nic);
					CONFIG_MSG("nv_path=%s\n", nv_path);
					return;
				}
			}
		}
	}
}
#endif
#endif

#ifdef BCMPCIE
static int
dhd_conf_read_otp_from_bp(si_t *sih, uint32 *data_buf,
	uint32 cis_start_addr, uint32 cis_max_cnt)
{
	int int_val = 0, i = 0, bp_idx = 0;
	int boardtype_backplane_addr[] = {
		0x18010324, /* OTP Control 1 */
		0x18012618, /* PMU min resource mask */
	};
	int boardtype_backplane_data[] = {
		0x00fa0000,
		0x0e4fffff /* Keep on ARMHTAVAIL */
	};
	uint32 org_boardtype_backplane_data[] = {
		0,
		0
	};

	for (bp_idx=0; bp_idx<ARRAYSIZE(boardtype_backplane_addr); bp_idx++) {
		/* Read OTP Control 1 and PMU min_rsrc_mask before writing */
		if (si_backplane_access(sih, boardtype_backplane_addr[bp_idx], sizeof(int),
				&org_boardtype_backplane_data[bp_idx], TRUE) != BCME_OK) {
			CONFIG_ERROR("invalid size/addr combination\n");
			return BCME_ERROR;
		}

		/* Write new OTP and PMU configuration */
		if (si_backplane_access(sih, boardtype_backplane_addr[bp_idx], sizeof(int),
				&boardtype_backplane_data[bp_idx], FALSE) != BCME_OK) {
			CONFIG_ERROR("invalid size/addr combination\n");
			return BCME_ERROR;
		}

		if (si_backplane_access(sih, boardtype_backplane_addr[bp_idx], sizeof(int),
				&int_val, TRUE) != BCME_OK) {
			CONFIG_ERROR("invalid size/addr combination\n");
			return BCME_ERROR;
		}

		CONFIG_TRACE("boardtype_backplane_addr 0x%08x rdata 0x%04x\n",
			boardtype_backplane_addr[bp_idx], int_val);
	}

	/* read tuple raw data */
	for (i=0; i<cis_max_cnt; i++) {
		if (si_backplane_access(sih, cis_start_addr + i * sizeof(uint32),
				sizeof(uint32),	&data_buf[i], TRUE) != BCME_OK) {
			break;
		}
		CONFIG_TRACE("tuple index %d, raw data 0x%08x\n", i,  data_buf[i]);
	}

	for (bp_idx=0; bp_idx<ARRAYSIZE(boardtype_backplane_addr); bp_idx++) {
		/* Write original OTP and PMU configuration */
		if (si_backplane_access(sih, boardtype_backplane_addr[bp_idx], sizeof(int),
				&org_boardtype_backplane_data[bp_idx], FALSE) != BCME_OK) {
			CONFIG_ERROR("invalid size/addr combination\n");
			return BCME_ERROR;
		}

		if (si_backplane_access(sih, boardtype_backplane_addr[bp_idx], sizeof(int),
				&int_val, TRUE) != BCME_OK) {
			CONFIG_ERROR("invalid size/addr combination\n");
			return BCME_ERROR;
		}

		CONFIG_TRACE("boardtype_backplane_addr 0x%08x rdata 0x%04x\n",
			boardtype_backplane_addr[bp_idx], int_val);
	}

	return i * sizeof(uint32);
}

int
dhd_conf_get_otp(dhd_pub_t *dhd, si_t *sih)
{
	int totlen, len;
	uint32 *raw_data = NULL;
	cis_tuple_format_t *tuple;
	uint32 cis_start_addr = 0, cis_end_addr = 0, cis_max_cnt;
	uint chip, chiprev;
	int i, ret = BCME_OK;

	chip = dhd->conf->chip;
	chiprev = dhd->conf->chiprev;

	for (i=0; i<sizeof(chip_cisaddr_map)/sizeof(chip_cisaddr_map[0]); i++) {
		const chip_cisaddr_map_t* row = &chip_cisaddr_map[i];
		if (row->chip == chip && row->chiprev == chiprev) {
			cis_start_addr = row->start_addr;
			cis_end_addr = row->end_addr;
		}
	}

	if (!cis_start_addr || !cis_end_addr) {
		CONFIG_TRACE("no matched chip\n");
		goto exit;
	}
	cis_max_cnt = (cis_end_addr - cis_start_addr + 1) / sizeof(uint32);

	raw_data = kmalloc(cis_max_cnt, GFP_KERNEL);
	if (raw_data == NULL) {
		CONFIG_ERROR("Failed to allocate buffer of %d bytes\n", cis_max_cnt);
		goto exit;
	}

	totlen = dhd_conf_read_otp_from_bp(sih, raw_data, cis_start_addr, cis_max_cnt);
	if (totlen == BCME_ERROR || totlen == 0) {
		CONFIG_ERROR("Can't read the OTP\n");
		ret = BCME_ERROR;
		goto exit;
	}

	tuple = (cis_tuple_format_t *)raw_data;

	if (config_msg_level & CONFIG_TRACE_LEVEL) {
		CONFIG_TRACE("start: 0x%x, end: 0x%x, totlen: %d\n",
			cis_start_addr, cis_end_addr, totlen);
		prhex("CIS", &tuple->id, totlen);
	}

	/* check the first tuple has tag 'start' */
	if (tuple->id != CIS_TUPLE_ID_BRCM) {
		CONFIG_ERROR("Can not find the TAG\n");
		ret = BCME_ERROR;
		goto exit;
	}

	/* find tagged parameter */
	while (totlen >= (tuple->len + CIS_TUPLE_HDR_LEN)) {
		len = tuple->len;
		if ((config_msg_level & CONFIG_TRACE_LEVEL) && tuple->id) {
			prhex("TPL", &tuple->id, tuple->len+CIS_TUPLE_HDR_LEN);
		}
		if ((tuple->id == CIS_TUPLE_ID_BRCM) &&
				(tuple->tag == CIS_TUPLE_TAG_MACADDR) &&
				(totlen >= (int)(len + CIS_TUPLE_HDR_LEN))) {
			memcpy(&dhd->conf->otp_mac, tuple->data, ETHER_ADDR_LEN);
		}
		tuple = (cis_tuple_format_t*)((uint8*)tuple + (len + CIS_TUPLE_HDR_LEN));
		totlen -= (len + CIS_TUPLE_HDR_LEN);
	}

exit:
	if(raw_data)
		kfree(raw_data);
	return ret;
}

bool
dhd_conf_legacy_msi_chip(dhd_pub_t *dhd)
{
	uint chip;

	chip = dhd->conf->chip;

	if (chip == BCM4354_CHIP_ID || chip == BCM4356_CHIP_ID ||
		chip == BCM4371_CHIP_ID ||
		chip == BCM4359_CHIP_ID) {
		return true;
	}

	return false;
}
#endif

void
dhd_conf_free_country_list(struct dhd_conf *conf)
{
	country_list_t *country = conf->country_head;
	int count = 0;

	CONFIG_TRACE("called\n");
	while (country) {
		CONFIG_TRACE("Free cspec %s\n", country->cspec.country_abbrev);
		conf->country_head = country->next;
		kfree(country);
		country = conf->country_head;
		count++;
	}
	CONFIG_TRACE("%d country released\n", count);
}

void
dhd_conf_free_mchan_list(struct dhd_conf *conf)
{
	mchan_params_t *mchan = conf->mchan;
	int count = 0;

	CONFIG_TRACE("called\n");
	while (mchan) {
		CONFIG_TRACE("Free cspec %p\n", mchan);
		conf->mchan = mchan->next;
		kfree(mchan);
		mchan = conf->mchan;
		count++;
	}
	CONFIG_TRACE("%d mchan released\n", count);
}

const chip_name_map_t*
dhd_conf_match_chip(dhd_pub_t *dhd, uint ag_type)
{
	uint chip, chiprev;
	int i;

	chip = dhd->conf->chip;
	chiprev = dhd->conf->chiprev;

	for (i=0; i<sizeof(chip_name_map)/sizeof(chip_name_map[0]); i++) {
		const chip_name_map_t* row = &chip_name_map[i];
		if (row->chip == chip && row->chiprev == chiprev &&
				(row->ag_type == ag_type ||
					ag_type == DONT_CARE || row->ag_type == DONT_CARE)) {
			return row;
		}
	}

	return NULL;
}

#ifdef UPDATE_MODULE_NAME
const module_name_map_t*
dhd_conf_match_module(dhd_pub_t *dhd)
{
	uint devid, chip, chiprev;
#ifdef BCMPCIE
	uint svid, ssid;
#endif
#if defined(BCMSDIO) || defined(BCMPCIE)
	int i;
#endif

	devid = dhd->conf->devid;
	chip = dhd->conf->chip;
	chiprev = dhd->conf->chiprev;
#ifdef BCMPCIE
	svid = dhd->conf->svid;
	ssid = dhd->conf->ssid;
#endif

#ifdef BCMSDIO
	for (i=0; i<sizeof(module_name_map)/sizeof(module_name_map[0]); i++) {
		const module_name_map_t* row = &module_name_map[i];
		if (row->devid == devid && row->chip == chip && row->chiprev == chiprev &&
				!strcmp(row->module_name, dhd->conf->module_name)) {
			return row;
		}
	}
#endif

#ifdef BCMPCIE
	for (i=0; i<sizeof(module_name_map)/sizeof(module_name_map[0]); i++) {
		const module_name_map_t* row = &module_name_map[i];
		if (row->devid == devid && row->chip == chip && row->chiprev == chiprev &&
				row->svid == svid && row->ssid == ssid) {
			return row;
		}
	}
#endif

	return NULL;
}
#endif

int
dhd_conf_set_fw_name_by_chip(dhd_pub_t *dhd, char *fw_path)
{
#ifdef UPDATE_MODULE_NAME
	const module_name_map_t* row_module = NULL;
#endif
	const chip_name_map_t* row_chip = NULL;
	int fw_type, ag_type;
	uint chip, chiprev;
	char *name_ptr;
	int i;

	chip = dhd->conf->chip;
	chiprev = dhd->conf->chiprev;

	if (fw_path[0] == '\0') {
#ifdef CONFIG_BCMDHD_FW_PATH
		bcm_strncpy_s(fw_path, MOD_PARAM_PATHLEN-1, CONFIG_BCMDHD_FW_PATH, MOD_PARAM_PATHLEN-1);
		if (fw_path[0] == '\0')
#endif
		{
			CONFIG_MSG("firmware path is null\n");
			return 0;
		}
	}
#ifndef FW_PATH_AUTO_SELECT
	return DONT_CARE;
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
	else if (strstr(name_ptr, "_ezmesh"))
		fw_type = FW_TYPE_EZMESH;
	else if (strstr(name_ptr, "_es"))
		fw_type = FW_TYPE_ES;
	else if (strstr(name_ptr, "_mfg"))
		fw_type = FW_TYPE_MFG;
	else if (strstr(name_ptr, "_minime"))
		fw_type = FW_TYPE_MINIME;
	else
		fw_type = FW_TYPE_STA;
#ifdef WLEASYMESH
	if (dhd->conf->fw_type == FW_TYPE_EZMESH)
		fw_type = FW_TYPE_EZMESH;
#endif /* WLEASYMESH */

	row_chip = dhd_conf_match_chip(dhd, ag_type);
	if (row_chip && strlen(row_chip->chip_name)) {
		strcpy(name_ptr, "fw_");
		strcat(name_ptr, row_chip->chip_name);
#ifdef BCMUSBDEV_COMPOSITE
		strcat(name_ptr, "_cusb");
#endif
		if (fw_type == FW_TYPE_APSTA)
			strcat(name_ptr, "_apsta.bin");
		else if (fw_type == FW_TYPE_P2P)
			strcat(name_ptr, "_p2p.bin");
		else if (fw_type == FW_TYPE_MESH)
			strcat(name_ptr, "_mesh.bin");
		else if (fw_type == FW_TYPE_EZMESH)
			strcat(name_ptr, "_ezmesh.bin");
		else if (fw_type == FW_TYPE_ES)
			strcat(name_ptr, "_es.bin");
		else if (fw_type == FW_TYPE_MFG)
			strcat(name_ptr, "_mfg.bin");
		else if (fw_type == FW_TYPE_MINIME)
			strcat(name_ptr, "_minime.bin");
		else
			strcat(name_ptr, ".bin");
	}

#ifdef UPDATE_MODULE_NAME
	row_module = dhd_conf_match_module(dhd);
	if (row_module && strlen(row_module->chip_name)) {
		strcpy(name_ptr, "fw_");
		strcat(name_ptr, row_module->chip_name);
#ifdef BCMUSBDEV_COMPOSITE
		strcat(name_ptr, "_cusb");
#endif
		if (fw_type == FW_TYPE_APSTA)
			strcat(name_ptr, "_apsta.bin");
		else if (fw_type == FW_TYPE_P2P)
			strcat(name_ptr, "_p2p.bin");
		else if (fw_type == FW_TYPE_MESH)
			strcat(name_ptr, "_mesh.bin");
		else if (fw_type == FW_TYPE_EZMESH)
			strcat(name_ptr, "_ezmesh.bin");
		else if (fw_type == FW_TYPE_ES)
			strcat(name_ptr, "_es.bin");
		else if (fw_type == FW_TYPE_MFG)
			strcat(name_ptr, "_mfg.bin");
		else if (fw_type == FW_TYPE_MINIME)
			strcat(name_ptr, "_minime.bin");
		else
			strcat(name_ptr, ".bin");
	}
#endif

	dhd->conf->fw_type = fw_type;

#ifndef MINIME
	if (fw_type == FW_TYPE_MINIME)
		CONFIG_ERROR("***** Please enable MINIME in Makefile *****\n");
#endif

	CONFIG_TRACE("firmware_path=%s\n", fw_path);
	return ag_type;
}

void
dhd_conf_set_clm_name_by_chip(dhd_pub_t *dhd, char *clm_path, int ag_type)
{
#ifdef UPDATE_MODULE_NAME
	const module_name_map_t* row_module = NULL;
#endif
	const chip_name_map_t* row_chip = NULL;
	uint chip, chiprev;
	char *name_ptr;
	int i;

	chip = dhd->conf->chip;
	chiprev = dhd->conf->chiprev;

	if (clm_path[0] == '\0') {
		CONFIG_MSG("clm path is null\n");
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

	row_chip = dhd_conf_match_chip(dhd, ag_type);
	if (row_chip && strlen(row_chip->chip_name)) {
		strcpy(name_ptr, "clm_");
		strcat(name_ptr, row_chip->chip_name);
		strcat(name_ptr, ".blob");
	}

#ifdef UPDATE_MODULE_NAME
	row_module = dhd_conf_match_module(dhd);
	if (row_module && strlen(row_module->chip_name)) {
		strcpy(name_ptr, "clm_");
		strcat(name_ptr, row_module->chip_name);
		strcat(name_ptr, ".blob");
	}
#endif

	CONFIG_TRACE("clm_path=%s\n", clm_path);
}

void
dhd_conf_set_nv_name_by_chip(dhd_pub_t *dhd, char *nv_path, int ag_type)
{
#if defined(BCMPCIE) && defined(UPDATE_MODULE_NAME)
	const module_name_map_t* row_module = NULL;
#endif
	const chip_name_map_t* row_chip = NULL;
	uint chip, chiprev;
	char *name_ptr, nv_name[32];
	int i;

	chip = dhd->conf->chip;
	chiprev = dhd->conf->chiprev;

	if (nv_path[0] == '\0') {
#ifdef CONFIG_BCMDHD_NVRAM_PATH
		bcm_strncpy_s(nv_path, MOD_PARAM_PATHLEN-1, CONFIG_BCMDHD_NVRAM_PATH, MOD_PARAM_PATHLEN-1);
		if (nv_path[0] == '\0')
#endif
		{
			CONFIG_MSG("nvram path is null\n");
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

	row_chip = dhd_conf_match_chip(dhd, ag_type);
	if (row_chip && strlen(row_chip->module_name)) {
		strcpy(name_ptr, "nvram_");
		strcat(name_ptr, row_chip->module_name);
#ifdef BCMUSBDEV_COMPOSITE
		strcat(name_ptr, "_cusb");
#endif
		strcat(name_ptr, ".txt");
	}
	strcpy(nv_name, name_ptr);

#if defined(BCMSDIO) && defined(GET_OTP_MODULE_NAME)
	if (strlen(dhd->conf->module_name)) {
		strcpy(name_ptr, "nvram_");
		strcat(name_ptr, dhd->conf->module_name);
		strcat(name_ptr, ".txt");
#ifdef COMPAT_OLD_MODULE
		if (dhd->conf->chip == BCM4359_CHIP_ID) {
			struct file *fp;
			// compatible for AP6398S and AP6398SA
			fp = filp_open(nv_path, O_RDONLY, 0);
			if (IS_ERR(fp)) {
				strcpy(name_ptr, nv_name);
			} else {
				filp_close((struct file *)fp, NULL);
			}
		}
#endif
	}
#endif

#if defined(BCMPCIE) && defined(UPDATE_MODULE_NAME)
	row_module = dhd_conf_match_module(dhd);
	if (row_module && strlen(row_module->module_name)) {
		strcpy(name_ptr, "nvram_");
		strcat(name_ptr, row_module->module_name);
		strcat(name_ptr, ".txt");
	}
#endif

	for (i=0; i<dhd->conf->nv_by_chip.count; i++) {
		if (chip==dhd->conf->nv_by_chip.m_chip_nv_path_head[i].chip &&
				chiprev==dhd->conf->nv_by_chip.m_chip_nv_path_head[i].chiprev) {
			strcpy(name_ptr, dhd->conf->nv_by_chip.m_chip_nv_path_head[i].name);
			break;
		}
	}

	CONFIG_TRACE("nvram_path=%s\n", nv_path);
}

void
dhd_conf_copy_path(dhd_pub_t *dhd, char *dst_name, char *dst_path, char *src_path)
{
	int i;

	if (src_path[0] == '\0') {
		CONFIG_MSG("src_path is null\n");
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

	CONFIG_TRACE("dst_path=%s\n", dst_path);
}

#ifdef CONFIG_PATH_AUTO_SELECT
void
dhd_conf_set_conf_name_by_chip(dhd_pub_t *dhd, char *conf_path)
{
#ifdef UPDATE_MODULE_NAME
	const module_name_map_t* row_module = NULL;
#endif
	const chip_name_map_t* row_chip = NULL;
	uint chip, chiprev;
	char *name_ptr;
	int i;

	chip = dhd->conf->chip;
	chiprev = dhd->conf->chiprev;

	if (conf_path[0] == '\0') {
		CONFIG_MSG("config path is null\n");
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

	row_chip = dhd_conf_match_chip(dhd, DONT_CARE);
	if (row_chip && strlen(row_chip->chip_name)) {
		strcpy(name_ptr, "config_");
		strcat(name_ptr, row_chip->chip_name);
		strcat(name_ptr, ".txt");
	}

#ifdef UPDATE_MODULE_NAME
	row_module = dhd_conf_match_module(dhd);
	if (row_module && strlen(row_module->chip_name)) {
		strcpy(name_ptr, "config_");
		strcat(name_ptr, row_module->chip_name);
		strcat(name_ptr, ".txt");
	}
#endif

	CONFIG_TRACE("config_path=%s\n", conf_path);
}
#endif

#ifdef TPUT_MONITOR
void
dhd_conf_tput_monitor(dhd_pub_t *dhd)
{
	struct dhd_conf *conf = dhd->conf;

	if (conf->tput_monitor_ms && conf->data_drop_mode >= FW_DROP) {
		if (conf->tput_ts.tv_sec == 0 && conf->tput_ts.tv_nsec == 0) {
			osl_do_gettimeofday(&conf->tput_ts);
		} else {
			struct osl_timespec cur_ts;
			int32 tput_tx = 0, tput_rx = 0, tput_tx_kb = 0,
				tput_rx_kb = 0, tput_net = 0, tput_net_kb = 0;
			uint32 diff_ms;
			unsigned long diff_bytes;
			osl_do_gettimeofday(&cur_ts);
			diff_ms = osl_do_gettimediff(&cur_ts, &conf->tput_ts)/1000;
			if (diff_ms >= conf->tput_monitor_ms) {
				diff_bytes = dhd->dstats.tx_bytes - conf->last_tx;
				tput_tx = (int32)((diff_bytes/1024/1024)*8)*1000/diff_ms;
				if (tput_tx == 0) {
					tput_tx = (int32)(diff_bytes*8/1024/1024)*1000/diff_ms;
					tput_tx_kb = (int32)(diff_bytes*8*1000/1024)/diff_ms;
					tput_tx_kb = tput_tx_kb % 1000;
				}
				diff_bytes = dhd->dstats.rx_bytes - conf->last_rx;
				tput_rx = (int32)((diff_bytes/1024/1024)*8)*1000/diff_ms;
				if (tput_rx == 0) {
					tput_rx = (int32)(diff_bytes*8/1024/1024)*1000/diff_ms;
					tput_rx_kb = (int32)(diff_bytes*8*1000/1024)/diff_ms;
					tput_rx_kb = tput_tx_kb % 1000;
				}
				diff_bytes = conf->net_len - conf->last_net_tx;
				tput_net = (int32)((diff_bytes/1024/1024)*8)*1000/diff_ms;
				if (tput_net == 0) {
					tput_net = (int32)(diff_bytes*8/1024/1024)*1000/diff_ms;
					tput_net_kb = (int32)(diff_bytes*8*1000/1024)/diff_ms;
					tput_net_kb = tput_net_kb % 1000;
				}
				conf->last_tx = dhd->dstats.tx_bytes;
				conf->last_rx = dhd->dstats.rx_bytes;
				conf->last_net_tx = conf->net_len;
				memcpy(&conf->tput_ts, &cur_ts, sizeof(struct osl_timespec));
				CONFIG_TRACE("xmit=%3d.%d%d%d Mbps, tx=%3d.%d%d%d Mbps, rx=%3d.%d%d%d Mbps\n",
					tput_net, (tput_net_kb/100)%10, (tput_net_kb/10)%10, (tput_net_kb)%10,
					tput_tx, (tput_tx_kb/100)%10, (tput_tx_kb/10)%10, (tput_tx_kb)%10,
					tput_rx, (tput_rx_kb/100)%10, (tput_rx_kb/10)%10, (tput_rx_kb)%10);
			}
		}
	}
}
#endif

#ifdef DHD_TPUT_PATCH
void
dhd_conf_set_tput_patch(dhd_pub_t *dhd)
{
	struct dhd_conf *conf = dhd->conf;

	if (conf->tput_patch) {
		conf->mtu = 1500;
/* set pktsetsum false by default since this will cause to
  * the checksum is wrong of downloaded file
*/
		conf->pktsetsum = FALSE;
#ifdef BCMSDIO
		conf->dhd_dpc_prio = 98;
/* need to check if CPU can support multi-core first,
 * so don't enable it by default.
 */
//		conf->dpc_cpucore = 2;
//		conf->rxf_cpucore = 3;
//		conf->disable_proptx = 1;
		conf->frameburst = 1;
#ifdef DYNAMIC_MAX_HDR_READ
		conf->max_hdr_read = 256;
		firstread = 256;
#endif /* DYNAMIC_MAX_HDR_READ */
		dhd_rxbound = 512;
#endif /* BCMSDIO */
#ifdef BCMPCIE
#if defined(SET_XPS_CPUS)
		conf->xps_cpus = TRUE;
#endif /* SET_XPS_CPUS */
#if defined(SET_RPS_CPUS)
		conf->rps_cpus = TRUE;
#endif /* SET_RPS_CPUS */
		conf->orphan_move = 3;
		conf->flow_ring_queue_threshold = 2048;
#endif /* BCMPCIE */
#ifdef DHDTCPACK_SUPPRESS
		conf->tcpack_sup_ratio = 15;
		conf->tcpack_sup_delay = 10;
#endif /* DHDTCPACK_SUPPRESS */
	}
	else {
		conf->mtu = 0;
		conf->pktsetsum = FALSE;
#ifdef BCMSDIO
		conf->dhd_dpc_prio = -1;
		conf->disable_proptx = -1;
		conf->frameburst = 1;
#ifdef DYNAMIC_MAX_HDR_READ
		conf->max_hdr_read = 0;
		firstread = 32;
#endif /* DYNAMIC_MAX_HDR_READ */
		dhd_rxbound = 128;
#endif /* BCMSDIO */
#ifdef BCMPCIE
#if defined(SET_XPS_CPUS)
		conf->xps_cpus = FALSE;
#endif /* SET_XPS_CPUS */
#if defined(SET_RPS_CPUS)
		conf->rps_cpus = FALSE;
#endif /* SET_RPS_CPUS */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
		conf->orphan_move = 1;
#else
		conf->orphan_move = 0;
#endif
		conf->flow_ring_queue_threshold = 2048;
#endif /* BCMPCIE */
#ifdef DHDTCPACK_SUPPRESS
		conf->tcpack_sup_ratio = CUSTOM_TCPACK_SUPP_RATIO;
		conf->tcpack_sup_delay = CUSTOM_TCPACK_DELAY_TIME;
#endif /* DHDTCPACK_SUPPRESS */
	}
}

void
dhd_conf_dump_tput_patch(dhd_pub_t *dhd)
{
	struct dhd_conf *conf = dhd->conf;

	CONFIG_TRACE("tput_patch=%d\n", conf->tput_patch);
	CONFIG_TRACE("mtu=%d\n", conf->mtu);
	CONFIG_TRACE("pktsetsum=%d\n", conf->pktsetsum);
	CONFIG_TRACE("orphan_move=%d\n", conf->orphan_move);
#ifdef DHDTCPACK_SUPPRESS
	CONFIG_TRACE("tcpack_sup_ratio=%d\n", conf->tcpack_sup_ratio);
	CONFIG_TRACE("tcpack_sup_delay=%d\n", conf->tcpack_sup_delay);
#endif

#ifdef BCMSDIO
	CONFIG_TRACE("dhd_dpc_prio=%d\n", conf->dhd_dpc_prio);
	CONFIG_TRACE("dhd_poll=%d\n", conf->dhd_poll);
	CONFIG_TRACE("disable_proptx=%d\n", conf->disable_proptx);
	CONFIG_TRACE("frameburst=%d\n", conf->frameburst);
#ifdef DYNAMIC_MAX_HDR_READ
	CONFIG_TRACE("max_hdr_read=%d\n", conf->max_hdr_read);
	CONFIG_TRACE("firstread=%d\n", firstread);
#endif
	CONFIG_TRACE("dhd_rxbound=%d\n", dhd_rxbound);
#endif

#ifdef BCMPCIE
	CONFIG_TRACE("flow_ring_queue_threshold=%d\n", conf->flow_ring_queue_threshold);
#endif

#if defined(SET_XPS_CPUS)
	CONFIG_TRACE("xps_cpus=%d\n", conf->xps_cpus);
#endif
#if defined(SET_RPS_CPUS)
	CONFIG_TRACE("rps_cpus=%d\n", conf->rps_cpus);
#endif

}
#endif /* DHD_TPUT_PATCH */

#ifdef DHD_LINUX_STD_FW_API
#define FIRMWARE_CLASS_PATH "/sys/module/firmware_class/parameters/path"
static int
dhd_conf_get_fw_path(char *path, int len)
{
	char *pch;
	int err, path_len = 0;

	err = dhd_read_file(FIRMWARE_CLASS_PATH, path, len);
	if(err < 0){
		CONFIG_ERROR("firmware path can not read %d\n", err);
	} else {
		pch = strchr(path, '\n');
		if (pch)
			*pch = '\0';
		CONFIG_TRACE("path = %s\n", path);
		path_len = strlen(path);
	}

	return path_len;
}

static void
dhd_conf_get_filename(char *pFilename)
{
	const char *pName = NULL;

	if ((pFilename) && (*pFilename)) {
		// back/reverse search the '/'
		pName = strrchr(pFilename, '/');
		if (NULL == pName) {
			pName = pFilename;
		} else {
			if (pName[1]) {
				pName++;
			} else {
				pName = NULL;
			}
		}
	}

	if (pName)
		strcpy(pFilename, pName);
	return;
}
#endif /* DHD_LINUX_STD_FW_API */

void
dhd_conf_set_path_params(dhd_pub_t *dhd, char *fw_path, char *nv_path)
{
	int ag_type;
#ifdef DHD_LINUX_STD_FW_API
	char path[WLC_IOCTL_SMLEN];
	int path_len;
#endif

	/* External conf takes precedence if specified */
	dhd_conf_preinit(dhd);

#ifdef DHD_LINUX_STD_FW_API
	// preprocess the filename to only left 'name'
	dhd_conf_get_filename(fw_path);
	dhd_conf_get_filename(nv_path);
	dhd_conf_get_filename(dhd->clm_path);
	dhd_conf_get_filename(dhd->conf_path);
#endif

	if (dhd->conf_path[0] == '\0') {
		dhd_conf_copy_path(dhd, "config.txt", dhd->conf_path, nv_path);
	}
	if (dhd->clm_path[0] == '\0') {
		dhd_conf_copy_path(dhd, "clm.blob", dhd->clm_path, fw_path);
	}
#ifdef CONFIG_PATH_AUTO_SELECT
	dhd_conf_set_conf_name_by_chip(dhd, dhd->conf_path);
#endif

	ag_type = dhd_conf_set_fw_name_by_chip(dhd, fw_path);
	dhd_conf_set_nv_name_by_chip(dhd, nv_path, ag_type);
	dhd_conf_set_clm_name_by_chip(dhd, dhd->clm_path, ag_type);
#ifdef SET_FWNV_BY_MAC
	dhd_conf_set_fw_name_by_mac(dhd, fw_path);
	dhd_conf_set_nv_name_by_mac(dhd, nv_path);
#endif

#ifdef DHD_LINUX_STD_FW_API
	memset(path, 0, sizeof(path));
	path_len = dhd_conf_get_fw_path(path, sizeof(path));
	snprintf(path+path_len, WLC_IOCTL_SMLEN, "%s", fw_path);
	CONFIG_MSG("Final fw_path=%s\n", path);
	snprintf(path+path_len, WLC_IOCTL_SMLEN, "%s", nv_path);
	CONFIG_MSG("Final nv_path=%s\n", path);
	snprintf(path+path_len, WLC_IOCTL_SMLEN, "%s", dhd->clm_path);
	CONFIG_MSG("Final clm_path=%s\n", path);
	snprintf(path+path_len, WLC_IOCTL_SMLEN, "%s", dhd->conf_path);
	CONFIG_MSG("Final conf_path=%s\n", path);
#else
	CONFIG_MSG("Final fw_path=%s\n", fw_path);
	CONFIG_MSG("Final nv_path=%s\n", nv_path);
	CONFIG_MSG("Final clm_path=%s\n", dhd->clm_path);
	CONFIG_MSG("Final conf_path=%s\n", dhd->conf_path);
#endif

	dhd_conf_read_config(dhd, dhd->conf_path);
#ifdef DHD_TPUT_PATCH
	dhd_conf_dump_tput_patch(dhd);
#endif
}

int
dhd_conf_set_intiovar(dhd_pub_t *dhd, int ifidx, uint cmd, char *name, int val,
	int def, bool down)
{
	int ret = -1;
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" + '\0' + bitvec  */

	if (val >= def) {
		if (down) {
			if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0)) < 0)
				CONFIG_ERROR("WLC_DOWN setting failed %d\n", ret);
		}
		if (cmd == WLC_SET_VAR) {
			CONFIG_TRACE("set %s %d\n", name, val);
			bcm_mkiovar(name, (char *)&val, sizeof(val), iovbuf, sizeof(iovbuf));
			if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
				CONFIG_ERROR("%s setting failed %d\n", name, ret);
		} else {
			CONFIG_TRACE("set %s %d %d\n", name, cmd, val);
			if ((ret = dhd_wl_ioctl_cmd(dhd, cmd, &val, sizeof(val), TRUE, 0)) < 0)
				CONFIG_ERROR("%s setting failed %d\n", name, ret);
		}
	}

	return ret;
}

static int
dhd_conf_set_bufiovar(dhd_pub_t *dhd, int ifidx, uint cmd, char *name,
	char *buf, int len, bool down)
{
	char iovbuf[WLC_IOCTL_SMLEN];
	s32 iovar_len;
	int ret = -1;

	if (down) {
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, ifidx)) < 0)
			CONFIG_ERROR("WLC_DOWN setting failed %d\n", ret);
	}

	if (cmd == WLC_SET_VAR) {
		iovar_len = bcm_mkiovar(name, buf, len, iovbuf, sizeof(iovbuf));
		if (iovar_len > 0)
			ret = dhd_wl_ioctl_cmd(dhd, cmd, iovbuf, iovar_len, TRUE, ifidx);
		else
			ret = BCME_BUFTOOSHORT;
		if (ret < 0)
			CONFIG_ERROR("%s setting failed %d, len=%d\n", name, ret, len);
	} else {
		if ((ret = dhd_wl_ioctl_cmd(dhd, cmd, buf, len, TRUE, ifidx)) < 0)
			CONFIG_ERROR("%s setting failed %d\n", name, ret);
	}

	return ret;
}

static int
dhd_conf_iovar_buf(dhd_pub_t *dhd, int ifidx, int cmd, char *name,
	char *buf, int len)
{
	char *iovbuf = NULL;
	int ret = -1, iovbuf_len = WLC_IOCTL_MEDLEN;
	s32 iovar_len;

	iovbuf = kmalloc(iovbuf_len, GFP_KERNEL);
	if (iovbuf == NULL) {
		CONFIG_ERROR("Failed to allocate buffer of %d bytes\n", iovbuf_len);
		goto exit;
	}

	if (cmd == WLC_GET_VAR) {
		if (bcm_mkiovar(name, buf, len, iovbuf, iovbuf_len)) {
			ret = dhd_wl_ioctl_cmd(dhd, cmd, iovbuf, iovbuf_len, FALSE, ifidx);
			if (!ret) {
				memcpy(buf, iovbuf, len);
			} else {
				CONFIG_ERROR("get iovar %s failed %d\n", name, ret);
			}
		} else {
			CONFIG_ERROR("mkiovar %s failed\n", name);
		}
	} else if (cmd == WLC_SET_VAR) {
		iovar_len = bcm_mkiovar(name, buf, len, iovbuf, iovbuf_len);
		if (iovar_len > 0)
			ret = dhd_wl_ioctl_cmd(dhd, cmd, iovbuf, iovar_len, TRUE, ifidx);
		else
			ret = BCME_BUFTOOSHORT;
		if (ret < 0)
			CONFIG_ERROR("%s setting failed %d, len=%d\n", name, ret, len);
	}

exit:
	if (iovbuf)
		kfree(iovbuf);
	return ret;
}

static int
dhd_conf_get_iovar(dhd_pub_t *dhd, int ifidx, int cmd, char *name,
	char *buf, int len)
{
	char iovbuf[WLC_IOCTL_SMLEN];
	int ret = -1;

	if (cmd == WLC_GET_VAR) {
		if (bcm_mkiovar(name, NULL, 0, iovbuf, sizeof(iovbuf))) {
			ret = dhd_wl_ioctl_cmd(dhd, cmd, iovbuf, sizeof(iovbuf), FALSE, ifidx);
			if (!ret) {
				memcpy(buf, iovbuf, len);
			} else {
				CONFIG_ERROR("get iovar %s failed %d\n", name, ret);
			}
		} else {
			CONFIG_ERROR("mkiovar %s failed\n", name);
		}
	} else {
		ret = dhd_wl_ioctl_cmd(dhd, cmd, buf, len, FALSE, 0);
		if (ret < 0)
			CONFIG_ERROR("get iovar %s failed %d\n", name, ret);
	}

	return ret;
}

static int
dhd_conf_rsdb_mode(dhd_pub_t *dhd, char *cmd, char *buf)
{
	wl_config_t rsdb_mode_cfg = {1, 0};

	if (buf) {
		rsdb_mode_cfg.config = (int)simple_strtol(buf, NULL, 0);
		CONFIG_MSG("rsdb_mode %d\n", rsdb_mode_cfg.config);
		dhd_conf_set_bufiovar(dhd, 0, WLC_SET_VAR, cmd, (char *)&rsdb_mode_cfg,
			sizeof(rsdb_mode_cfg), TRUE);
	}

	return 0;
}

int
dhd_conf_reg2args(dhd_pub_t *dhd, char *cmd, bool set, uint32 index, uint32 *val)
{
	char var[WLC_IOCTL_SMLEN];
	uint32 int_val, len;
	void *ptr = NULL;
	int ret = 0;

	len = sizeof(int_val);
	int_val = htod32(index);
	memset(var, 0, sizeof(var));
	memcpy(var, (char *)&int_val, sizeof(int_val));

	if (set) {
		int_val = htod32(*val);
		memcpy(&var[len], (char *)&int_val, sizeof(int_val));
		len += sizeof(int_val);
		dhd_conf_iovar_buf(dhd, 0, WLC_SET_VAR, cmd, var, sizeof(var));
	} else {
		ret = dhd_conf_iovar_buf(dhd, 0, WLC_GET_VAR, cmd, var, sizeof(var));
		if (ret < 0)
			return ret;
		ptr = var;
		*val = dtoh32(*(int *)ptr);
	}

	return ret;
}

static int
dhd_conf_btc_params(dhd_pub_t *dhd, char *cmd, char *buf)
{
	int ret = BCME_OK;
	uint32 cur_val;
	int index = 0, mask = 0, value = 0;
	// btc_params=[index] [mask] [value]
	// Ex: btc_params=82 0x0021 0x0001

	if (buf) {
		sscanf(buf, "%d %x %x", &index, &mask, &value);
	}

	CONFIG_TRACE("%s%d mask=0x%04x value=0x%04x\n", cmd, index, mask, value);

	ret = dhd_conf_reg2args(dhd, cmd, FALSE, index, &cur_val);
	CONFIG_TRACE("%s%d = 0x%04x\n", cmd, index, cur_val);
	cur_val &= (~mask);
	cur_val |= value;

	// need to WLC_UP before btc_params
	dhd_conf_set_intiovar(dhd, 0, WLC_UP, "WLC_UP", 0, 0, FALSE);

	CONFIG_TRACE("wl %s%d 0x%04x\n", cmd, index, cur_val);
	ret = dhd_conf_reg2args(dhd, cmd, TRUE, index, &cur_val);

	ret = dhd_conf_reg2args(dhd, cmd, FALSE, index, &cur_val);
	CONFIG_MSG("%s%d = 0x%04x\n", cmd, index, cur_val);

	return ret;
}

typedef struct sub_cmd_t {
	char *name;
	uint16 id;		/* id for the dongle f/w switch/case  */
	uint16 type;		/* base type of argument IOVT_XXXX */
} sub_cmd_t;

/* wl he sub cmd list */
static const sub_cmd_t he_cmd_list[] = {
	{"enab", WL_HE_CMD_ENAB, IOVT_UINT8},
	{"features", WL_HE_CMD_FEATURES, IOVT_UINT32},
	{"bsscolor", WL_HE_CMD_BSSCOLOR, IOVT_UINT8},
	{"partialbsscolor", WL_HE_CMD_PARTIAL_BSSCOLOR, IOVT_UINT8},
	{"cap", WL_HE_CMD_CAP, IOVT_UINT8},
	{"staid", WL_HE_CMD_STAID, IOVT_UINT16},
	{"rtsdurthresh", WL_HE_CMD_RTSDURTHRESH, IOVT_UINT16},
	{"peduration", WL_HE_CMD_PEDURATION, IOVT_UINT8},
	{"testbed_mode", WL_HE_CMD_TESTBED_MODE, IOVT_UINT32},
	{"omi_ulmu_throttle", WL_HE_CMD_OMI_ULMU_THROTTLE, IOVT_UINT16},
	{"omi_dlmu_rr_mpf_map", WL_HE_CMD_OMI_DLMU_RSD_RCM_MPF_MAP, IOVT_UINT32},
	{"ulmu_disable_policy", WL_HE_CMD_ULMU_DISABLE_POLICY, IOVT_UINT8},
	{"sr_prohibit", WL_HE_CMD_SR_PROHIBIT, IOVT_UINT8},
};

static uint
wl_he_iovt2len(uint iovt)
{
	switch (iovt) {
	case IOVT_BOOL:
	case IOVT_INT8:
	case IOVT_UINT8:
		return sizeof(uint8);
	case IOVT_INT16:
	case IOVT_UINT16:
		return sizeof(uint16);
	case IOVT_INT32:
	case IOVT_UINT32:
		return sizeof(uint32);
	default:
		/* ASSERT(0); */
		return 0;
	}
}

static int
dhd_conf_he_cmd(dhd_pub_t * dhd, char *cmd, char *buf)
{
	int ret = BCME_OK, i;
	bcm_xtlv_t *pxtlv = NULL;
	uint8 mybuf[128];
	uint16 he_id = -1, he_len = 0, mybuf_len = sizeof(mybuf);
	uint32 he_val;
	const sub_cmd_t *tpl = he_cmd_list;
	char sub_cmd[32], he_val_str[10];

	if (buf) {
		sscanf(buf, "%s %s", sub_cmd, he_val_str);
	}

	for (i=0; i<ARRAY_SIZE(he_cmd_list); i++, tpl++) {
		if (!strcmp(tpl->name, sub_cmd)) {
			he_id = tpl->id;
			he_len = wl_he_iovt2len(tpl->type);
			break;
		}
	}
	if (he_id < 0) {
		CONFIG_ERROR("No he id found for %s\n", sub_cmd);
		return 0;
	}

	pxtlv = (bcm_xtlv_t *)mybuf;

	if (strlen(he_val_str)) {
		he_val = simple_strtol(he_val_str, NULL, 0);
		ret = bcm_pack_xtlv_entry((uint8**)&pxtlv, &mybuf_len, he_id,
			he_len, (uint8 *)&he_val, BCM_XTLV_OPTION_ALIGN32);
		if (ret != BCME_OK) {
			CONFIG_ERROR("failed to pack he enab, err: %s\n", bcmerrorstr(ret));
			return 0;
		}
		CONFIG_TRACE("he %s 0x%x\n", sub_cmd, he_val);
		dhd_conf_set_bufiovar(dhd, 0, WLC_SET_VAR, cmd, (char *)&mybuf,
			sizeof(mybuf), TRUE);
	}

	return 0;
}

#ifndef SUPPORT_RANDOM_MAC_SCAN
int
dhd_conf_scan_mac(dhd_pub_t * dhd, char *cmd, char *buf)
{
	uint8 buffer[WLC_IOCTL_SMLEN] = {0, };
	wl_scanmac_t *sm = NULL;
	wl_scanmac_enable_t *sm_enable = NULL;
	int enable = 0, len = 0, ret = -1;
	char sub_cmd[32], iovbuf[WLC_IOCTL_SMLEN];
	s32 iovar_len;

	memset(sub_cmd, 0, sizeof(sub_cmd));
	if (buf) {
		sscanf(buf, "%s %d", sub_cmd, &enable);
	}

	if (!strcmp(sub_cmd, "enable")) {
		sm = (wl_scanmac_t *)buffer;
		sm_enable = (wl_scanmac_enable_t *)sm->data;
		sm->len = sizeof(*sm_enable);
		sm_enable->enable = enable;
		len = OFFSETOF(wl_scanmac_t, data) + sm->len;
		sm->subcmd_id = WL_SCANMAC_SUBCMD_ENABLE;
		CONFIG_TRACE("scanmac enable %d\n", sm_enable->enable);

		iovar_len = bcm_mkiovar("scanmac", buffer, len, iovbuf, sizeof(iovbuf));
		if (iovar_len > 0)
			ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, iovar_len, TRUE, 0);
		else
			ret = BCME_BUFTOOSHORT;
		if (ret == BCME_UNSUPPORTED)
			CONFIG_TRACE("scanmac, UNSUPPORTED\n");
		else if (ret != BCME_OK)
			CONFIG_ERROR("%s setting failed %d, len=%d\n", "scanmac", ret, len);
	}
	else {
		CONFIG_ERROR("wrong cmd \"%s %d\"\n", sub_cmd, enable);
	}

	return 0;
}
#endif

typedef int (tpl_parse_t)(dhd_pub_t *dhd, char *name, char *buf);

typedef struct iovar_tpl_t {
	int cmd;
	char *name;
	tpl_parse_t *parse;
} iovar_tpl_t;

const iovar_tpl_t iovar_tpl_list[] = {
	{WLC_SET_VAR,	"rsdb_mode",	dhd_conf_rsdb_mode},
	{WLC_SET_VAR,	"he",		dhd_conf_he_cmd},
	{WLC_SET_VAR,	"btc_params",	dhd_conf_btc_params},
#ifndef SUPPORT_RANDOM_MAC_SCAN
	{WLC_SET_VAR,	"scanmac",		dhd_conf_scan_mac},
#endif
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
		ret = tpl->parse(dhd, name, buf);
	} else {
		ret = -1;
	}

	return ret;
}

static bool
dhd_conf_set_wl_cmd(dhd_pub_t *dhd, char *data, bool down)
{
	int cmd, val, ret = 0, len;
	char name[32], *pch, *pick_tmp, *pick_tmp2, *pdata = NULL;

	/* Process wl_preinit:
	 * wl_preinit=[cmd]=[val], [cmd]=[val]
	 * Ex: wl_preinit=86=0, mpc=0
	 */

	if (data == NULL)
		return FALSE;

	len = strlen(data);
	pdata = kmalloc(len+1, GFP_KERNEL);
	if (pdata == NULL) {
		CONFIG_ERROR("Failed to allocate buffer of %d bytes\n", len+1);
		goto exit;
	}
	memset(pdata, 0, len+1);
	strcpy(pdata, data);

	pick_tmp = pdata;
	while (pick_tmp && (pick_tmp2 = bcmstrtok(&pick_tmp, ",", 0)) != NULL) {
		char *pEnd;
		pch = bcmstrtok(&pick_tmp2, "=", 0);
		if (!pch)
			break;
		if (*pch == ' ') {
			pch++;
		}
		memset(name, 0 , sizeof (name));
		cmd = bcm_strtoul(pch, &pEnd, 0);
		if (cmd == 0 || strlen(pEnd)) {
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
			dhd_conf_set_intiovar(dhd, 0, cmd, name, val, -1, down);
		}
	}

exit:
	if (pdata)
		kfree(pdata);
	return true;
}

int
dhd_conf_get_band(dhd_pub_t *dhd)
{
	int band = -1;

	if (dhd && dhd->conf)
		band = dhd->conf->band;
	else
		CONFIG_ERROR("dhd or conf is NULL\n");

	return band;
}

int
dhd_conf_get_country(dhd_pub_t *dhd, wl_country_t *cspec)
{
	int bcmerror = -1;

	memset(cspec, 0, sizeof(wl_country_t));
	bcm_mkiovar("country", NULL, 0, (char*)cspec, sizeof(wl_country_t));
	if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, cspec, sizeof(wl_country_t),
			FALSE, 0)) < 0)
		CONFIG_ERROR("country code getting failed %d\n", bcmerror);

	return bcmerror;
}

int
dhd_conf_map_country_list(dhd_pub_t *dhd, wl_country_t *cspec)
{
	int bcmerror = -1;
	struct dhd_conf *conf = dhd->conf;
	country_list_t *country = conf->country_head;

#ifdef CCODE_LIST
	bcmerror = dhd_ccode_map_country_list(dhd, cspec);
#endif
	// **:XZ/11 => return XZ/11 if not found
	// **:**/0 => return user specified ccode if not found, but set regrev 0
	while (country != NULL) {
		if (!strncmp("**", country->cspec.country_abbrev, 2)) {
			if (!strncmp("**", country->cspec.ccode, 2)) {
				cspec->rev = 0;
				bcmerror = 0;
				break;
			}
			memcpy(cspec->ccode, country->cspec.ccode, WLC_CNTRY_BUF_SZ);
			cspec->rev = country->cspec.rev;
			bcmerror = 0;
			break;
		} else if (!strncmp(cspec->country_abbrev,
				country->cspec.country_abbrev, 2)) {
			memcpy(cspec->ccode, country->cspec.ccode, WLC_CNTRY_BUF_SZ);
			cspec->rev = country->cspec.rev;
			bcmerror = 0;
			break;
		}
		country = country->next;
	}

	if (!bcmerror)
		CONFIG_MSG("%s/%d\n", cspec->ccode, cspec->rev);

	return bcmerror;
}

int
dhd_conf_set_country(dhd_pub_t *dhd, wl_country_t *cspec)
{
	int bcmerror = -1;

	memset(&dhd->dhd_cspec, 0, sizeof(wl_country_t));

	CONFIG_MSG("set country %s, revision %d\n", cspec->ccode, cspec->rev);
	bcmerror = dhd_conf_set_bufiovar(dhd, 0, WLC_SET_VAR, "country", (char *)cspec,
		sizeof(wl_country_t), FALSE);
	dhd_conf_get_country(dhd, cspec);
	CONFIG_MSG("Country code: %s (%s/%d)\n",
		cspec->country_abbrev, cspec->ccode, cspec->rev);

	return bcmerror;
}

int
dhd_conf_fix_country(dhd_pub_t *dhd)
{
	int bcmerror = -1;
	int band;
	wl_uint32_list_t *list;
	u8 valid_chan_list[sizeof(u32)*(WL_NUMCHANNELS + 1)];
	wl_country_t cspec;

	if (!(dhd && dhd->conf)) {
		return bcmerror;
	}

	memset(valid_chan_list, 0, sizeof(valid_chan_list));
	list = (wl_uint32_list_t *)(void *) valid_chan_list;
	list->count = htod32(WL_NUMCHANNELS);
	if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_GET_VALID_CHANNELS, valid_chan_list,
			sizeof(valid_chan_list), FALSE, 0)) < 0) {
		CONFIG_ERROR("get channels failed with %d\n", bcmerror);
	}

	band = dhd_conf_get_band(dhd);

	if (bcmerror || ((band==WLC_BAND_AUTO || band==WLC_BAND_2G || band==-1) &&
			dtoh32(list->count)<11)) {
		CONFIG_ERROR("bcmerror=%d, # of channels %d\n",
			bcmerror, dtoh32(list->count));
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
		CONFIG_ERROR("dhd or conf is NULL\n");
	}

	return match;
}

int
dhd_conf_set_roam(dhd_pub_t *dhd)
{
	int bcmerror = -1;
	struct dhd_conf *conf = dhd->conf;
	uint wnm_bsstrans_resp = 0;

	if (dhd->conf->chip == BCM4359_CHIP_ID) {
		dhd_conf_get_iovar(dhd, 0, WLC_GET_VAR, "wnm_bsstrans_resp",
			(char *)&wnm_bsstrans_resp, sizeof(wnm_bsstrans_resp));
		if (wnm_bsstrans_resp == WL_BSSTRANS_POLICY_PRODUCT) {
			dhd->wbtext_policy = WL_BSSTRANS_POLICY_ROAM_ALWAYS;
			dhd_conf_set_intiovar(dhd, 0, WLC_SET_VAR, "wnm_bsstrans_resp",
				WL_BSSTRANS_POLICY_ROAM_ALWAYS, 0, FALSE);
		}
	}

	dhd_roam_disable = conf->roam_off;
	dhd_conf_set_intiovar(dhd, 0, WLC_SET_VAR, "roam_off", dhd->conf->roam_off, 0, FALSE);

	if (!conf->roam_off || !conf->roam_off_suspend) {
		CONFIG_MSG("set roam_trigger %d\n", conf->roam_trigger[0]);
		dhd_conf_set_bufiovar(dhd, 0, WLC_SET_ROAM_TRIGGER, "WLC_SET_ROAM_TRIGGER",
			(char *)conf->roam_trigger, sizeof(conf->roam_trigger), FALSE);

		CONFIG_MSG("set roam_scan_period %d\n", conf->roam_scan_period[0]);
		dhd_conf_set_bufiovar(dhd, 0, WLC_SET_ROAM_SCAN_PERIOD, "WLC_SET_ROAM_SCAN_PERIOD",
			(char *)conf->roam_scan_period, sizeof(conf->roam_scan_period), FALSE);

		CONFIG_MSG("set roam_delta %d\n", conf->roam_delta[0]);
		dhd_conf_set_bufiovar(dhd, 0, WLC_SET_ROAM_DELTA, "WLC_SET_ROAM_DELTA",
			(char *)conf->roam_delta, sizeof(conf->roam_delta), FALSE);

		dhd_conf_set_intiovar(dhd, 0, WLC_SET_VAR, "fullroamperiod",
			dhd->conf->fullroamperiod, 1, FALSE);
	}

	return bcmerror;
}

void
dhd_conf_add_to_eventbuffer(struct eventmsg_buf *ev, u16 event, bool set)
{
	if (!ev || (event > WLC_E_LAST))
		return;

	if (ev->num < MAX_EVENT_BUF_NUM) {
		ev->event[ev->num].type = event;
		ev->event[ev->num].set = set;
		ev->num++;
	} else {
		CONFIG_ERROR("evenbuffer doesn't support > %u events. Update"
			" the define MAX_EVENT_BUF_NUM \n", MAX_EVENT_BUF_NUM);
		ASSERT(0);
	}
}

s32
dhd_conf_apply_eventbuffer(dhd_pub_t *dhd, eventmsg_buf_t *ev)
{
	char eventmask[WL_EVENTING_MASK_LEN];
	int i, ret = 0;

	if (!ev || (!ev->num))
		return -EINVAL;

	/* Read event_msgs mask */
	ret = dhd_conf_get_iovar(dhd, 0, WLC_GET_VAR, "event_msgs", eventmask,
		sizeof(eventmask));
	if (unlikely(ret)) {
		CONFIG_ERROR("Get event_msgs error (%d)\n", ret);
		goto exit;
	}

	/* apply the set bits */
	for (i = 0; i < ev->num; i++) {
		if (ev->event[i].set)
			setbit(eventmask, ev->event[i].type);
		else
			clrbit(eventmask, ev->event[i].type);
	}

	/* Write updated Event mask */
	ret = dhd_conf_set_bufiovar(dhd, 0, WLC_SET_VAR, "event_msgs", eventmask,
		sizeof(eventmask), FALSE);
	if (unlikely(ret)) {
		CONFIG_ERROR("Set event_msgs error (%d)\n", ret);
	}

exit:
	return ret;
}

static int
dhd_conf_enable_roam_offload(dhd_pub_t *dhd, int enable)
{
	int err;
	eventmsg_buf_t ev_buf;

	if (dhd->conf->roam_off_suspend)
		return 0;

	err = dhd_conf_set_intiovar(dhd, 0, WLC_SET_VAR, "roam_offload", enable, 0, FALSE);
	if (err)
		return err;

	bzero(&ev_buf, sizeof(eventmsg_buf_t));
	dhd_conf_add_to_eventbuffer(&ev_buf, WLC_E_PSK_SUP, !enable);
	dhd_conf_add_to_eventbuffer(&ev_buf, WLC_E_ASSOC_REQ_IE, !enable);
	dhd_conf_add_to_eventbuffer(&ev_buf, WLC_E_ASSOC_RESP_IE, !enable);
	dhd_conf_add_to_eventbuffer(&ev_buf, WLC_E_REASSOC, !enable);
	dhd_conf_add_to_eventbuffer(&ev_buf, WLC_E_JOIN, !enable);
	dhd_conf_add_to_eventbuffer(&ev_buf, WLC_E_ROAM, !enable);
	err = dhd_conf_apply_eventbuffer(dhd, &ev_buf);

	CONFIG_TRACE("roam_offload %d\n", enable);

	return err;
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
		CONFIG_MSG("set bw_cap 2g 0x%x\n", param.bw_cap);
		dhd_conf_set_bufiovar(dhd, 0, WLC_SET_VAR, "bw_cap", (char *)&param,
			sizeof(param), TRUE);
	}

	if (dhd->conf->bw_cap[1] >= 0) {
		memset(&param, 0, sizeof(param));
		param.band = WLC_BAND_5G;
		param.bw_cap = (uint)dhd->conf->bw_cap[1];
		CONFIG_MSG("set bw_cap 5g 0x%x\n", param.bw_cap);
		dhd_conf_set_bufiovar(dhd, 0, WLC_SET_VAR, "bw_cap", (char *)&param,
			sizeof(param), TRUE);
	}
}

void
dhd_conf_get_wme(dhd_pub_t *dhd, int ifidx, int mode, edcf_acparam_t *acp)
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
	if ((bcmerror = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, iovbuf, sizeof(iovbuf),
			FALSE, ifidx)) < 0) {
		CONFIG_ERROR("wme_ac_sta getting failed %d\n", bcmerror);
		return;
	}
	memcpy((char*)acp, iovbuf, sizeof(edcf_acparam_t)*AC_COUNT);

	acparam = &acp[AC_BK];
	CONFIG_TRACE("BK: aci %d aifsn %d ecwmin %d ecwmax %d txop 0x%x\n",
		acparam->ACI, acparam->ACI&EDCF_AIFSN_MASK,
		acparam->ECW&EDCF_ECWMIN_MASK, (acparam->ECW&EDCF_ECWMAX_MASK)>>EDCF_ECWMAX_SHIFT,
		acparam->TXOP);
	acparam = &acp[AC_BE];
	CONFIG_TRACE("BE: aci %d aifsn %d ecwmin %d ecwmax %d txop 0x%x\n",
		acparam->ACI, acparam->ACI&EDCF_AIFSN_MASK,
		acparam->ECW&EDCF_ECWMIN_MASK, (acparam->ECW&EDCF_ECWMAX_MASK)>>EDCF_ECWMAX_SHIFT,
		acparam->TXOP);
	acparam = &acp[AC_VI];
	CONFIG_TRACE("VI: aci %d aifsn %d ecwmin %d ecwmax %d txop 0x%x\n",
		acparam->ACI, acparam->ACI&EDCF_AIFSN_MASK,
		acparam->ECW&EDCF_ECWMIN_MASK, (acparam->ECW&EDCF_ECWMAX_MASK)>>EDCF_ECWMAX_SHIFT,
		acparam->TXOP);
	acparam = &acp[AC_VO];
	CONFIG_TRACE("VO: aci %d aifsn %d ecwmin %d ecwmax %d txop 0x%x\n",
		acparam->ACI, acparam->ACI&EDCF_AIFSN_MASK,
		acparam->ECW&EDCF_ECWMIN_MASK, (acparam->ECW&EDCF_ECWMAX_MASK)>>EDCF_ECWMAX_SHIFT,
		acparam->TXOP);

	return;
}

void
dhd_conf_update_wme(dhd_pub_t *dhd, int ifidx, int mode,
	edcf_acparam_t *acparam_cur, int aci)
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

	CONFIG_MSG("wme_ac %s aci %d aifsn %d ecwmin %d ecwmax %d txop 0x%x\n",
		mode?"ap":"sta", acp->ACI, acp->ACI&EDCF_AIFSN_MASK,
		acp->ECW&EDCF_ECWMIN_MASK, (acp->ECW&EDCF_ECWMAX_MASK)>>EDCF_ECWMAX_SHIFT,
		acp->TXOP);

	/*
	* Now use buf as an output buffer.
	* Put WME acparams after "wme_ac\0" in buf.
	* NOTE: only one of the four ACs can be set at a time.
	*/
	if (mode == 0)
		dhd_conf_set_bufiovar(dhd, ifidx, WLC_SET_VAR, "wme_ac_sta", (char *)acp,
			sizeof(edcf_acparam_t), FALSE);
	else
		dhd_conf_set_bufiovar(dhd, ifidx, WLC_SET_VAR, "wme_ac_ap", (char *)acp,
			sizeof(edcf_acparam_t), FALSE);

}

void
dhd_conf_set_wme(dhd_pub_t *dhd, int ifidx, int mode)
{
	edcf_acparam_t acparam_cur[AC_COUNT];

	if (dhd && dhd->conf) {
		if (!dhd->conf->force_wme_ac) {
			CONFIG_TRACE("force_wme_ac is not enabled %d\n",
				dhd->conf->force_wme_ac);
			return;
		}

		CONFIG_TRACE("Before change:\n");
		dhd_conf_get_wme(dhd, ifidx, mode, acparam_cur);

		dhd_conf_update_wme(dhd, ifidx, mode, &acparam_cur[AC_BK], AC_BK);
		dhd_conf_update_wme(dhd, ifidx, mode, &acparam_cur[AC_BE], AC_BE);
		dhd_conf_update_wme(dhd, ifidx, mode, &acparam_cur[AC_VI], AC_VI);
		dhd_conf_update_wme(dhd, ifidx, mode, &acparam_cur[AC_VO], AC_VO);

		CONFIG_TRACE("After change:\n");
		dhd_conf_get_wme(dhd, ifidx, mode, acparam_cur);
	} else {
		CONFIG_ERROR("dhd or conf is NULL\n");
	}

	return;
}

void
dhd_conf_set_mchan_bw(dhd_pub_t *dhd, int p2p_mode, int miracast_mode)
{
	struct dhd_conf *conf = dhd->conf;
	mchan_params_t *mchan = conf->mchan;
	bool set = true;

	while (mchan != NULL) {
		set = true;
		set &= (mchan->bw >= 0);
		set &= ((mchan->p2p_mode == -1) | (mchan->p2p_mode == p2p_mode));
		set &= ((mchan->miracast_mode == -1) | (mchan->miracast_mode == miracast_mode));
		if (set) {
			dhd_conf_set_intiovar(dhd, 0, WLC_SET_VAR, "mchan_bw", mchan->bw, 0, FALSE);
		}
		mchan = mchan->next;
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

	/*  0) suspend_mode=1
	 * Case 0: default is unicast pkt and event wake up
	 * Case 1: no connection in suspend
	 *   1) wl_suspend=3=0
	 *   2) wl_resume=2=0
	 *   3) insuspend=0x7
	 * Case 2: keep connection in suspend, but no pkt and event wake up
	 *   1) dhd_master_mode=1
	 *   2) pkt_filter_delete=100, 102, 103, 104, 105, 106, 107
	 *   3) pkt_filter_add=141 0 0 0 0xFFFFFFFFFFFF 0x000000000000
	 *   4) insuspend=0x7
	 *   5) rekey_offload=1
	 * Case 3: magic pkt and event wake up
	 *   1) dhd_master_mode=1
	 *   2) pkt_filter_delete=100, 102, 103, 104, 105, 106, 107
	 *   3) pkt_filter_add=141 0 0 0 0xFFFFFFFFFFFF 0x000000000000
	 *   4) magic_pkt_filter_add=141 0 1 12
	 *   5) rekey_offload=1
	 */
	for(i=0; i<dhd->conf->pkt_filter_add.count; i++) {
		dhd->pktfilter[i+dhd->pktfilter_count] = dhd->conf->pkt_filter_add.filter[i];
		CONFIG_MSG("%s\n", dhd->pktfilter[i+dhd->pktfilter_count]);
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
				CONFIG_MSG("%d\n", dhd->conf->pkt_filter_del.id[i]);
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

int
dhd_conf_check_hostsleep(dhd_pub_t *dhd, int cmd, void *buf, int len,
	int *hostsleep_set, int *hostsleep_val, int *ret)
{
	if (dhd->conf->insuspend & (NO_TXCTL_IN_SUSPEND | WOWL_IN_SUSPEND)) {
		if (cmd == WLC_SET_VAR) {
			char *psleep = NULL;
			psleep = strstr(buf, "hostsleep");
			if (psleep) {
				*hostsleep_set = 1;
				memcpy(hostsleep_val, psleep+strlen("hostsleep")+1, sizeof(int));
			}
		}
		if (dhd->hostsleep && (!*hostsleep_set || *hostsleep_val)) {
			CONFIG_TRACE("block all none hostsleep clr cmd\n");
			*ret = BCME_EPERM;
			goto exit;
		} else if (*hostsleep_set && *hostsleep_val) {
			CONFIG_TRACE("hostsleep %d => %d\n", dhd->hostsleep, *hostsleep_val);
			dhd->hostsleep = *hostsleep_val;
			if (dhd->conf->insuspend & NO_TXDATA_IN_SUSPEND) {
				dhd_txflowcontrol(dhd, ALL_INTERFACES, ON);
			}
			if (dhd->hostsleep == 2) {
				*ret = 0;
				goto exit;
			}
		} else if (dhd->hostsleep == 2 && !*hostsleep_val) {
			CONFIG_TRACE("hostsleep %d => %d\n", dhd->hostsleep, *hostsleep_val);
			dhd->hostsleep = *hostsleep_val;
			if (dhd->conf->insuspend & NO_TXDATA_IN_SUSPEND) {
				dhd_txflowcontrol(dhd, ALL_INTERFACES, OFF);
			}
			*ret = 0;
			goto exit;
		}
	}
#ifdef NO_POWER_SAVE
	if (cmd == WLC_SET_PM) {
		if (*(const u32*)buf != 0) {
			CONFIG_TRACE("skip PM\n");
			*ret = BCME_OK;
			goto exit;
		}
	} else if (cmd == WLC_SET_VAR) {
		int cmd_len = strlen("mpc");
		if (!strncmp(buf, "mpc", cmd_len)) {
			if (*((u32 *)((u8*)buf+cmd_len+1)) != 0) {
				CONFIG_TRACE("skip mpc\n");
				*ret = BCME_OK;
				goto exit;
			}
		}
	}
#endif

	return 0;
exit:
	return -1;
}

void
dhd_conf_get_hostsleep(dhd_pub_t *dhd,
	int hostsleep_set, int hostsleep_val, int ret)
{
	if (dhd->conf->insuspend & (NO_TXCTL_IN_SUSPEND | WOWL_IN_SUSPEND)) {
		if (hostsleep_set) {
			if (hostsleep_val && ret) {
				CONFIG_TRACE("reset hostsleep %d => 0\n", dhd->hostsleep);
				dhd->hostsleep = 0;
				if (dhd->conf->insuspend & NO_TXDATA_IN_SUSPEND) {
					dhd_txflowcontrol(dhd, ALL_INTERFACES, OFF);
				}
			} else if (!hostsleep_val && !ret) {
				CONFIG_TRACE("set hostsleep %d => 0\n", dhd->hostsleep);
				dhd->hostsleep = 0;
				if (dhd->conf->insuspend & NO_TXDATA_IN_SUSPEND) {
					dhd_txflowcontrol(dhd, ALL_INTERFACES, OFF);
				}
			}
		}
	}
}

#ifdef WL_EXT_WOWL
#define WL_WOWL_TCPFIN	(1 << 26)
typedef struct wl_wowl_pattern2 {
	char cmd[4];
	wl_wowl_pattern_t wowl_pattern;
} wl_wowl_pattern2_t;
static int
dhd_conf_wowl_pattern(dhd_pub_t *dhd, int ifidx, bool add, char *data)
{
	uint buf_len = 0;
	int	id, type, polarity, offset;
	char cmd[4]="\0", mask[128]="\0", pattern[128]="\0", mask_tmp[128]="\0", *pmask_tmp;
	uint32 masksize, patternsize, pad_len = 0;
	wl_wowl_pattern2_t *wowl_pattern2 = NULL;
	char *mask_and_pattern;
	int ret = 0, i, j, v;

	if (data) {
		if (add)
			strcpy(cmd, "add");
		else
			strcpy(cmd, "clr");
		if (!strcmp(cmd, "clr")) {
			CONFIG_TRACE("wowl_pattern clr\n");
			ret = dhd_conf_set_bufiovar(dhd, ifidx, WLC_SET_VAR, "wowl_pattern", cmd,
				sizeof(cmd), FALSE);
			goto exit;
		}
		sscanf(data, "%d %d %d %d %s %s", &id, &type, &polarity, &offset,
			mask_tmp, pattern);
		masksize = strlen(mask_tmp) -2;
		CONFIG_TRACE("0 mask_tmp=%s, masksize=%d\n", mask_tmp, masksize);

		// add pading
		if (masksize % 16)
			pad_len = (16 - masksize % 16);
		for (i=0; i<pad_len; i++)
			strcat(mask_tmp, "0");
		masksize += pad_len;
		CONFIG_TRACE("1 mask_tmp=%s, masksize=%d\n", mask_tmp, masksize);

		// translate 0x00 to 0, others to 1
		j = 0;
		pmask_tmp = &mask_tmp[2];
		for (i=0; i<masksize/2; i++) {
			if(strncmp(&pmask_tmp[i*2], "00", 2))
				pmask_tmp[j] = '1';
			else
				pmask_tmp[j] = '0';
			j++;
		}
		pmask_tmp[j] = '\0';
		masksize = masksize / 2;
		CONFIG_TRACE("2 mask_tmp=%s, masksize=%d\n", mask_tmp, masksize);

		// reorder per 8bits
		pmask_tmp = &mask_tmp[2];
		for (i=0; i<masksize/8; i++) {
			char c;
			for (j=0; j<4; j++) {
				c = pmask_tmp[i*8+j];
				pmask_tmp[i*8+j] = pmask_tmp[(i+1)*8-j-1];
				pmask_tmp[(i+1)*8-j-1] = c;
			}
		}
		CONFIG_TRACE("3 mask_tmp=%s, masksize=%d\n", mask_tmp, masksize);

		// translate 8bits to 1byte
		j = 0; v = 0;
		pmask_tmp = &mask_tmp[2];
		strcpy(mask, "0x");
		for (i=0; i<masksize; i++) {
			v = (v<<1) | (pmask_tmp[i]=='1');
			if (((i+1)%4) == 0) {
				if (v < 10)
					mask[j+2] = v + '0';
				else
					mask[j+2] = (v-10) + 'a';
				j++;
				v = 0;
			}
		}
		mask[j+2] = '\0';
		masksize = j/2;
		CONFIG_TRACE("4 mask=%s, masksize=%d\n", mask, masksize);

		patternsize = (strlen(pattern)-2)/2;
		buf_len = sizeof(wl_wowl_pattern2_t) + patternsize + masksize;
		wowl_pattern2 = kmalloc(buf_len, GFP_KERNEL);
		if (wowl_pattern2 == NULL) {
			CONFIG_ERROR("Failed to allocate buffer of %d bytes\n", buf_len);
			goto exit;
		}
		memset(wowl_pattern2, 0, sizeof(wl_wowl_pattern2_t));

		strncpy(wowl_pattern2->cmd, cmd, sizeof(cmd));
		wowl_pattern2->wowl_pattern.id = id;
		wowl_pattern2->wowl_pattern.type = 0;
		wowl_pattern2->wowl_pattern.offset = offset;
		mask_and_pattern = (char*)wowl_pattern2 + sizeof(wl_wowl_pattern2_t);

		wowl_pattern2->wowl_pattern.masksize = masksize;
		ret = wl_pattern_atoh(mask, mask_and_pattern);
		if (ret == -1) {
			CONFIG_ERROR("rejecting mask=%s\n", mask);
			goto exit;
		}

		mask_and_pattern += wowl_pattern2->wowl_pattern.masksize;
		wowl_pattern2->wowl_pattern.patternoffset = sizeof(wl_wowl_pattern_t) +
			wowl_pattern2->wowl_pattern.masksize;

		wowl_pattern2->wowl_pattern.patternsize = patternsize;
		ret = wl_pattern_atoh(pattern, mask_and_pattern);
		if (ret == -1) {
			CONFIG_ERROR("rejecting pattern=%s\n", pattern);
			goto exit;
		}

		CONFIG_TRACE("%s %d %s %s\n", cmd, offset, mask, pattern);

		ret = dhd_conf_set_bufiovar(dhd, ifidx, WLC_SET_VAR, "wowl_pattern",
			(char *)wowl_pattern2, buf_len, FALSE);
	}

exit:
	if (wowl_pattern2)
		kfree(wowl_pattern2);
	return ret;
}

static int
dhd_conf_wowl_wakeind(dhd_pub_t *dhd, int ifidx, bool clear)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	wl_wowl_wakeind_t *wake = NULL;
	int ret = -1;
	char clr[6]="clear", wakeind_str[32]="\0";

	if (clear) {
		CONFIG_TRACE("wowl_wakeind clear\n");
		ret = dhd_conf_set_bufiovar(dhd, ifidx, WLC_SET_VAR, "wowl_wakeind",
			clr, sizeof(clr), 0);
	} else {
		ret = dhd_conf_get_iovar(dhd, ifidx, WLC_GET_VAR, "wowl_wakeind",
			iovar_buf, sizeof(iovar_buf));
		if (!ret) {
			wake = (wl_wowl_wakeind_t *) iovar_buf;
			if (wake->ucode_wakeind & WL_WOWL_MAGIC)
				strcpy(wakeind_str, "(MAGIC packet)");
			if (wake->ucode_wakeind & WL_WOWL_NET)
				strcpy(wakeind_str, "(Netpattern)");
			if (wake->ucode_wakeind & WL_WOWL_DIS)
				strcpy(wakeind_str, "(Disassoc/Deauth)");
			if (wake->ucode_wakeind & WL_WOWL_BCN)
				strcpy(wakeind_str, "(Loss of beacon)");
			if (wake->ucode_wakeind & WL_WOWL_TCPKEEP_TIME)
				strcpy(wakeind_str, "(TCPKA timeout)");
			if (wake->ucode_wakeind & WL_WOWL_TCPKEEP_DATA)
				strcpy(wakeind_str, "(TCPKA data)");
			if (wake->ucode_wakeind & WL_WOWL_TCPFIN)
				strcpy(wakeind_str, "(TCP FIN)");
			CONFIG_MSG("wakeind=0x%x %s\n", wake->ucode_wakeind, wakeind_str);
		}
	}

	return ret;
}
#endif

int
dhd_conf_mkeep_alive(dhd_pub_t *dhd, int ifidx, int id, int period,
	char *packet, bool bcast)
{
	wl_mkeep_alive_pkt_t *mkeep_alive_pktp;
	int ret = 0, len_bytes=0, buf_len=0;
	char *buf = NULL, *iovar_buf = NULL;
	uint8 *pdata;

	CONFIG_TRACE("id=%d, period=%d, packet=%s\n", id, period, packet);
	if (period >= 0) {
		buf = kmalloc(WLC_IOCTL_SMLEN, GFP_KERNEL);
		if (buf == NULL) {
			CONFIG_ERROR("Failed to allocate buffer of %d bytes\n", WLC_IOCTL_SMLEN);
			goto exit;
		}
		iovar_buf = kmalloc(WLC_IOCTL_SMLEN, GFP_KERNEL);
		if (iovar_buf == NULL) {
			CONFIG_ERROR("Failed to allocate buffer of %d bytes\n", WLC_IOCTL_SMLEN);
			goto exit;
		}
		mkeep_alive_pktp = (wl_mkeep_alive_pkt_t *)buf;
		mkeep_alive_pktp->version = htod16(WL_MKEEP_ALIVE_VERSION);
		mkeep_alive_pktp->length = htod16(WL_MKEEP_ALIVE_FIXED_LEN);
		mkeep_alive_pktp->keep_alive_id = id;
		buf_len += WL_MKEEP_ALIVE_FIXED_LEN;
		mkeep_alive_pktp->period_msec = period;
		if (packet && strlen(packet)) {
			len_bytes = wl_pattern_atoh(packet, (char *)mkeep_alive_pktp->data);
			buf_len += len_bytes;
			if (bcast) {
				memcpy(mkeep_alive_pktp->data, &ether_bcast, ETHER_ADDR_LEN);
			}
			ret = dhd_conf_get_iovar(dhd, ifidx, WLC_GET_VAR, "cur_etheraddr",
				iovar_buf, WLC_IOCTL_SMLEN);
			if (!ret) {
				pdata = mkeep_alive_pktp->data;
				memcpy(pdata+6, iovar_buf, ETHER_ADDR_LEN);
			}
		}
		mkeep_alive_pktp->len_bytes = htod16(len_bytes);
		ret = dhd_conf_set_bufiovar(dhd, ifidx, WLC_SET_VAR, "mkeep_alive",
			buf, buf_len, FALSE);
	}

exit:
	if (buf)
		kfree(buf);
	if (iovar_buf)
		kfree(iovar_buf);
	return ret;
}

#ifdef ARP_OFFLOAD_SUPPORT
void
dhd_conf_set_garp(dhd_pub_t *dhd, int ifidx, uint32 ipa, bool enable)
{
	int i, len = 0, total_len = WLC_IOCTL_SMLEN;
	char *iovar_buf = NULL, *packet = NULL;

	if (!dhd->conf->garp || ifidx != 0 || !(dhd->op_mode & DHD_FLAG_STA_MODE))
		return;

	CONFIG_TRACE("enable=%d\n", enable);

	if (enable) {
		iovar_buf = kmalloc(total_len, GFP_KERNEL);
		if (iovar_buf == NULL) {
			CONFIG_ERROR("Failed to allocate buffer of %d bytes\n", total_len);
			goto exit;
		}
		packet = kmalloc(total_len, GFP_KERNEL);
		if (packet == NULL) {
			CONFIG_ERROR("Failed to allocate buffer of %d bytes\n", total_len);
			goto exit;
		}
		dhd_conf_get_iovar(dhd, ifidx, WLC_GET_VAR, "cur_etheraddr", iovar_buf, total_len);

		len += snprintf(packet+len, total_len, "0xffffffffffff");
		for (i=0; i<ETHER_ADDR_LEN; i++)
			len += snprintf(packet+len, total_len, "%02x", iovar_buf[i]);
		len += snprintf(packet+len, total_len, "08060001080006040001");
		 // Sender Hardware Addr.
		for (i=0; i<ETHER_ADDR_LEN; i++)
			len += snprintf(packet+len, total_len, "%02x", iovar_buf[i]);
		 // Sender IP Addr.
		len += snprintf(packet+len, total_len, "%02x%02x%02x%02x",
			ipa&0xff, (ipa>>8)&0xff, (ipa>>16)&0xff, (ipa>>24)&0xff);
		 // Target Hardware Addr.
		len += snprintf(packet+len, total_len, "ffffffffffff");
		 // Target IP Addr.
		len += snprintf(packet+len, total_len, "%02x%02x%02x%02x",
			ipa&0xff, (ipa>>8)&0xff, (ipa>>16)&0xff, (ipa>>24)&0xff);
		len += snprintf(packet+len, total_len, "000000000000000000000000000000000000");
	}

	dhd_conf_mkeep_alive(dhd, ifidx, 0, dhd->conf->keep_alive_period, packet, TRUE);

exit:
	if (iovar_buf)
		kfree(iovar_buf);
	if (packet)
		kfree(packet);
	return;
}
#endif

uint
dhd_conf_get_insuspend(dhd_pub_t *dhd, uint mask)
{
	uint insuspend = 0;

	if (dhd->op_mode & DHD_FLAG_STA_MODE) {
		insuspend = dhd->conf->insuspend &
			(NO_EVENT_IN_SUSPEND | NO_TXDATA_IN_SUSPEND | NO_TXCTL_IN_SUSPEND |
			ROAM_OFFLOAD_IN_SUSPEND | WOWL_IN_SUSPEND);
	} else if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
		insuspend = dhd->conf->insuspend &
			(NO_EVENT_IN_SUSPEND | NO_TXDATA_IN_SUSPEND | NO_TXCTL_IN_SUSPEND |
			AP_DOWN_IN_SUSPEND | AP_FILTER_IN_SUSPEND);
	}

	return (insuspend & mask);
}

static void
dhd_conf_check_connection(dhd_pub_t *dhd, int ifidx, int suspend)
{
	struct dhd_conf *conf = dhd->conf;
	struct ether_addr bssid;
	wl_event_msg_t msg;
	int pm;
#ifdef WL_CFG80211
	struct net_device *net;
	unsigned long flags = 0;
#endif /* defined(WL_CFG80211) */

	if (suspend) {
		memset(&bssid, 0, ETHER_ADDR_LEN);
		dhd_wl_ioctl_cmd(dhd, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN, FALSE, ifidx);
		if (memcmp(&ether_null, &bssid, ETHER_ADDR_LEN))
			memcpy(&conf->bssid_insuspend, &bssid, ETHER_ADDR_LEN);
		else
			memset(&conf->bssid_insuspend, 0, ETHER_ADDR_LEN);
	}
	else {
		if (memcmp(&ether_null, &conf->bssid_insuspend, ETHER_ADDR_LEN)) {
			memset(&bssid, 0, ETHER_ADDR_LEN);
			dhd_wl_ioctl_cmd(dhd, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN, FALSE, ifidx);
			if (memcmp(&ether_null, &bssid, ETHER_ADDR_LEN)) {
				dhd_conf_set_intiovar(dhd, ifidx, WLC_SET_PM, "WLC_SET_PM", 0, 0, FALSE);
				dhd_conf_set_bufiovar(dhd, ifidx, WLC_SET_VAR, "send_nulldata",
					(char *)&bssid, ETHER_ADDR_LEN, FALSE);
				OSL_SLEEP(100);
				if (conf->pm >= 0)
					pm = conf->pm;
				else
					pm = PM_FAST;
				dhd_conf_set_intiovar(dhd, ifidx, WLC_SET_PM, "WLC_SET_PM", pm, 0, FALSE);
			} else {
				CONFIG_TRACE("send WLC_E_DEAUTH_IND event\n");
				bzero(&msg, sizeof(wl_event_msg_t));
				msg.ifidx = ifidx;
				memcpy(&msg.addr, &conf->bssid_insuspend, ETHER_ADDR_LEN);
				msg.event_type = hton32(WLC_E_DEAUTH_IND);
				msg.status = 0;
				msg.reason = hton32(DOT11_RC_DEAUTH_LEAVING);
#ifdef WL_EVENT
				wl_ext_event_send(dhd->event_params, &msg, NULL);
#endif
#ifdef WL_CFG80211
				spin_lock_irqsave(&dhd->up_lock, flags);
				net = dhd_idx2net(dhd, ifidx);
				if (net && dhd->up) {
					wl_cfg80211_event(net, &msg, NULL);
				}
				spin_unlock_irqrestore(&dhd->up_lock, flags);
#endif /* defined(WL_CFG80211) */
			}
		}
	}
}

#ifdef SUSPEND_EVENT
static void
dhd_conf_set_suspend_event(dhd_pub_t *dhd, int suspend)
{
	struct dhd_conf *conf = dhd->conf;
	char suspend_eventmask[WL_EVENTING_MASK_LEN];

	CONFIG_TRACE("Enter\n");
	if (suspend) {
#ifdef PROP_TXSTATUS
#if defined(BCMSDIO) || defined(BCMDBUS)
		if (dhd->wlfc_enabled) {
			dhd_wlfc_deinit(dhd);
			conf->wlfc = TRUE;
		} else {
			conf->wlfc = FALSE;
		}
#endif /* BCMSDIO || BCMDBUS */
#endif /* PROP_TXSTATUS */
		dhd_conf_get_iovar(dhd, 0, WLC_GET_VAR, "event_msgs",
			conf->resume_eventmask, sizeof(conf->resume_eventmask));
		memset(suspend_eventmask, 0, sizeof(suspend_eventmask));
		setbit(suspend_eventmask, WLC_E_ESCAN_RESULT);
		dhd_conf_set_bufiovar(dhd, 0, WLC_SET_VAR, "event_msgs",
			suspend_eventmask, sizeof(suspend_eventmask), FALSE);
	}
	else {
		dhd_conf_set_bufiovar(dhd, 0, WLC_SET_VAR, "event_msgs",
			conf->resume_eventmask, sizeof(conf->resume_eventmask), FALSE);
#ifdef PROP_TXSTATUS
#if defined(BCMSDIO) || defined(BCMDBUS)
		if (conf->wlfc) {
			dhd_wlfc_init(dhd);
			dhd_conf_set_intiovar(dhd, 0, WLC_UP, "WLC_UP", 0, 0, FALSE);
		}
#endif
#endif /* PROP_TXSTATUS */
	}

}
#endif

int
dhd_conf_suspend_resume_sta(dhd_pub_t *dhd, int ifidx, int suspend)
{
	struct dhd_conf *conf = dhd->conf;
	uint insuspend = 0;
	int pm;
#ifdef WL_EXT_WOWL
	int i;
#endif

	insuspend = dhd_conf_get_insuspend(dhd, ALL_IN_SUSPEND);
	if (insuspend)
		WL_MSG(dhd_ifname(dhd, ifidx), "suspend %d\n", suspend);

	if (suspend) {
		dhd_conf_check_connection(dhd, ifidx, suspend);
		dhd_conf_set_intiovar(dhd, ifidx, WLC_SET_VAR, "roam_off",
			conf->roam_off_suspend, 0, FALSE);
		dhd_conf_set_intiovar(dhd, ifidx, WLC_SET_VAR, "bcn_li_dtim",
			conf->suspend_bcn_li_dtim, 0, FALSE);
		if (conf->pm_in_suspend >= 0)
			pm = conf->pm_in_suspend;
		else if (conf->pm >= 0)
			pm = conf->pm;
		else
			pm = PM_FAST;
		dhd_conf_set_intiovar(dhd, ifidx, WLC_SET_PM, "WLC_SET_PM", pm, 0, FALSE);
#ifdef WL_EXT_WOWL
		if ((insuspend & WOWL_IN_SUSPEND) && dhd_master_mode) {
			dhd_conf_wowl_pattern(dhd, ifidx, FALSE, "clr");
			for(i=0; i<conf->pkt_filter_add.count; i++) {
				dhd_conf_wowl_pattern(dhd, ifidx, TRUE, conf->pkt_filter_add.filter[i]);
			}
			dhd_conf_set_intiovar(dhd, ifidx, WLC_SET_VAR, "wowl", conf->wowl, 0, FALSE);
			dhd_conf_set_intiovar(dhd, ifidx, WLC_SET_VAR, "wowl_activate", 1, 0, FALSE);
			dhd_conf_wowl_wakeind(dhd, ifidx, TRUE);
		}
#endif
	}
	else {
		dhd_conf_get_iovar(dhd, 0, WLC_GET_PM, "WLC_GET_PM", (char *)&pm, sizeof(pm));
		CONFIG_TRACE("PM in suspend = %d\n", pm);
		if (conf->pm >= 0)
			pm = conf->pm;
		else
			pm = PM_FAST;
		dhd_conf_set_intiovar(dhd, ifidx, WLC_SET_PM, "WLC_SET_PM", pm, 0, FALSE);
#ifdef WL_EXT_WOWL
		if (insuspend & WOWL_IN_SUSPEND) {
			dhd_conf_wowl_wakeind(dhd, ifidx, FALSE);
			dhd_conf_set_intiovar(dhd, ifidx, WLC_SET_VAR, "wowl_activate", 0, 0, FALSE);
			dhd_conf_set_intiovar(dhd, ifidx, WLC_SET_VAR, "wowl", 0, 0, FALSE);
			dhd_conf_wowl_pattern(dhd, ifidx, FALSE, "clr");
		}
#endif
		dhd_conf_set_intiovar(dhd, ifidx, WLC_SET_VAR, "bcn_li_dtim", 0, 0, FALSE);
		dhd_conf_set_intiovar(dhd, ifidx, WLC_SET_VAR, "roam_off",
			conf->roam_off, 0, FALSE);
		dhd_conf_check_connection(dhd, ifidx, suspend);
	}

	return 0;
}

#ifndef WL_EXT_IAPSTA
static int
dhd_conf_suspend_resume_ap(dhd_pub_t *dhd, int ifidx, int suspend)
{
	struct dhd_conf *conf = dhd->conf;
	uint insuspend = 0;

	insuspend = dhd_conf_get_insuspend(dhd, ALL_IN_SUSPEND);
	if (insuspend)
		WL_MSG(dhd_ifname(dhd, ifidx), "suspend %d\n", suspend);

	if (suspend) {
		if (insuspend & AP_DOWN_IN_SUSPEND) {
			dhd_conf_set_intiovar(dhd, ifidx, WLC_DOWN, "WLC_DOWN", 1, 0, FALSE);
		}
	} else {
		if (insuspend & AP_DOWN_IN_SUSPEND) {
			dhd_conf_set_intiovar(dhd, ifidx, WLC_UP, "WLC_UP", 0, 0, FALSE);
		}
	}

	return 0;
}
#endif /* !WL_EXT_IAPSTA */

static int
dhd_conf_suspend_resume_bus(dhd_pub_t *dhd, int suspend)
{
	uint insuspend = 0;

	insuspend = dhd_conf_get_insuspend(dhd, ALL_IN_SUSPEND);
	if (insuspend)
		CONFIG_MSG("suspend %d\n", suspend);

	if (suspend) {
		if (insuspend & (WOWL_IN_SUSPEND | NO_TXCTL_IN_SUSPEND)) {
#ifdef BCMSDIO
			uint32 intstatus = 0;
			int ret = 0;
#endif
			int hostsleep = 2;
#ifdef WL_EXT_WOWL
			hostsleep = 1;
#endif
			dhd_conf_set_intiovar(dhd, 0, WLC_SET_VAR, "hostsleep", hostsleep, 0, FALSE);
#ifdef BCMSDIO
			ret = dhd_bus_sleep(dhd, TRUE, &intstatus);
			CONFIG_TRACE("ret = %d, intstatus = 0x%x\n", ret, intstatus);
#endif
		}
	} else {
		if (insuspend & (WOWL_IN_SUSPEND | NO_TXCTL_IN_SUSPEND)) {
			dhd_conf_set_intiovar(dhd, 0, WLC_SET_VAR, "hostsleep", 0, 0, FALSE);
		}
	}

	return 0;
}

int
dhd_conf_set_suspend_resume(dhd_pub_t *dhd, int suspend)
{
	struct dhd_conf *conf = dhd->conf;
	uint insuspend = 0;

	insuspend = dhd_conf_get_insuspend(dhd, ALL_IN_SUSPEND);
	if (insuspend)
		CONFIG_MSG("op_mode %d, suspend %d, suspended %d, insuspend 0x%x, suspend_mode=%d\n",
			dhd->op_mode, suspend, conf->suspended, insuspend, conf->suspend_mode);

	if (conf->suspended == suspend || !dhd->up) {
		return 0;
	}

	if (suspend) {
		if (insuspend & (NO_EVENT_IN_SUSPEND|NO_TXCTL_IN_SUSPEND|WOWL_IN_SUSPEND)) {
			if (conf->suspend_mode == PM_NOTIFIER)
#ifdef WL_EXT_IAPSTA
				wl_iapsta_wait_event_complete(dhd);
#else
				wl_ext_wait_event_complete(dhd, 0);
#endif /* WL_EXT_IAPSTA */
		}
		if (insuspend & NO_TXDATA_IN_SUSPEND) {
			dhd_txflowcontrol(dhd, ALL_INTERFACES, ON);
		}
#if defined(WL_CFG80211) || defined(WL_ESCAN)
		if (insuspend & (NO_EVENT_IN_SUSPEND|NO_TXCTL_IN_SUSPEND|WOWL_IN_SUSPEND)) {
			if (conf->suspend_mode == PM_NOTIFIER)
				wl_ext_user_sync(dhd, 0, TRUE);
		}
#endif
		if (insuspend & ROAM_OFFLOAD_IN_SUSPEND)
			dhd_conf_enable_roam_offload(dhd, 2);
#ifdef SUSPEND_EVENT
		if (insuspend & NO_EVENT_IN_SUSPEND) {
			dhd_conf_set_suspend_event(dhd, suspend);
		}
#endif
#ifdef WL_EXT_IAPSTA
		wl_iapsta_suspend_resume(dhd, suspend);
#else
		if (dhd->op_mode & DHD_FLAG_STA_MODE) {
			dhd_conf_suspend_resume_sta(dhd, 0, suspend);
		} else if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
			dhd_conf_suspend_resume_ap(dhd, 0, suspend);
		}
#endif /* WL_EXT_IAPSTA */
		dhd_conf_set_wl_cmd(dhd, conf->wl_suspend, FALSE);
		dhd_conf_suspend_resume_bus(dhd, suspend);
		conf->suspended = TRUE;
	}
	else {
		dhd_conf_suspend_resume_bus(dhd, suspend);
#ifdef SUSPEND_EVENT
		if (insuspend & NO_EVENT_IN_SUSPEND) {
			dhd_conf_set_suspend_event(dhd, suspend);
		}
#endif
		if (insuspend & ROAM_OFFLOAD_IN_SUSPEND)
			dhd_conf_enable_roam_offload(dhd, 0);
		dhd_conf_set_wl_cmd(dhd, conf->wl_resume, FALSE);
#ifdef WL_EXT_IAPSTA
		wl_iapsta_suspend_resume(dhd, suspend);
#else
		if (dhd->op_mode & DHD_FLAG_STA_MODE) {
			dhd_conf_suspend_resume_sta(dhd, 0, suspend);
		} else if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
			dhd_conf_suspend_resume_ap(dhd, 0, suspend);
		}
#endif /* WL_EXT_IAPSTA */
#if defined(WL_CFG80211) || defined(WL_ESCAN)
		if (insuspend & (NO_EVENT_IN_SUSPEND|NO_TXCTL_IN_SUSPEND|WOWL_IN_SUSPEND)) {
			if (conf->suspend_mode == PM_NOTIFIER)
				wl_ext_user_sync(dhd, 0, FALSE);
		}
#endif
		if (insuspend & NO_TXDATA_IN_SUSPEND) {
			dhd_txflowcontrol(dhd, ALL_INTERFACES, OFF);
		}
		conf->suspended = FALSE;
	}

	return 0;
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

	CONFIG_MSG("fw_proptx=%d, disable_proptx=%d\n", fw_proptx, disable_proptx);

	return disable_proptx;
}
#endif

uint
pick_config_vars(char *varbuf, uint len, uint start_pos, char *pickbuf, int picklen)
{
	bool findNewline, changenewline=FALSE, pick=FALSE;
	int column;
	uint n, pick_column=0;

	findNewline = FALSE;
	column = 0;

	if (start_pos >= len) {
		CONFIG_ERROR("wrong start pos\n");
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
			if (pick_column >= picklen)
				break;
			pickbuf[pick_column] = varbuf[n];
			pick_column++;
		}
	}

	return n; // return current position
}

bool
dhd_conf_read_chiprev(dhd_pub_t *dhd, int *chip_match,
	char *full_param, uint len_param)
{
	char *data = full_param+len_param, *pick_tmp, *pch;
	uint chip = 0, rev = 0;

	/* Process chip, regrev:
	 * chip=[chipid], rev==[rev]
	 * Ex: chip=0x4359, rev=9
	 */
	if (!strncmp("chip=", full_param, len_param)) {
		chip = (int)simple_strtol(data, NULL, 0);
		pick_tmp = data;
		pch = bcmstrstr(pick_tmp, "rev=");
		if (pch) {
			rev = (int)simple_strtol(pch+strlen("rev="), NULL, 0);
		}
		if (chip == dhd->conf->chip && rev == dhd->conf->chiprev)
			*chip_match = 1;
		else
			*chip_match = 0;
		CONFIG_MSG("chip=0x%x, rev=%d, chip_match=%d\n", chip, rev, *chip_match);
	}

	return TRUE;
}

bool
dhd_conf_read_log_level(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	char *data = full_param+len_param;

	if (!strncmp("dhd_msg_level=", full_param, len_param)) {
		dhd_msg_level = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("dhd_msg_level = 0x%X\n", dhd_msg_level);
	}
	else if (!strncmp("dump_msg_level=", full_param, len_param)) {
		dump_msg_level = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("dump_msg_level = 0x%X\n", dump_msg_level);
	}
#ifdef BCMSDIO
	else if (!strncmp("sd_msglevel=", full_param, len_param)) {
		sd_msglevel = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("sd_msglevel = 0x%X\n", sd_msglevel);
	}
#endif
#ifdef BCMDBUS
	else if (!strncmp("dbus_msglevel=", full_param, len_param)) {
		dbus_msglevel = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("dbus_msglevel = 0x%X\n", dbus_msglevel);
	}
#endif
	else if (!strncmp("android_msg_level=", full_param, len_param)) {
		android_msg_level = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("android_msg_level = 0x%X\n", android_msg_level);
	}
	else if (!strncmp("config_msg_level=", full_param, len_param)) {
		config_msg_level = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("config_msg_level = 0x%X\n", config_msg_level);
	}
#ifdef WL_CFG80211
	else if (!strncmp("wl_dbg_level=", full_param, len_param)) {
		wl_dbg_level = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("wl_dbg_level = 0x%X\n", wl_dbg_level);
	}
#endif
#if defined(WL_WIRELESS_EXT)
	else if (!strncmp("iw_msg_level=", full_param, len_param)) {
		iw_msg_level = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("iw_msg_level = 0x%X\n", iw_msg_level);
	}
#endif
#if defined(DHD_DEBUG)
	else if (!strncmp("dhd_console_ms=", full_param, len_param)) {
		dhd->dhd_console_ms = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("dhd_console_ms = %d\n", dhd->dhd_console_ms);
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
		CONFIG_MSG("ac_val=%d, aifsn=%d\n", ac_val, wme->aifsn[ac_val]);
	}
	pick_tmp = pick;
	pch = bcmstrstr(pick_tmp, "ecwmin ");
	if (pch) {
		wme->ecwmin[ac_val] = (int)simple_strtol(pch+strlen("ecwmin "), NULL, 0);
		CONFIG_MSG("ac_val=%d, ecwmin=%d\n", ac_val, wme->ecwmin[ac_val]);
	}
	pick_tmp = pick;
	pch = bcmstrstr(pick_tmp, "ecwmax ");
	if (pch) {
		wme->ecwmax[ac_val] = (int)simple_strtol(pch+strlen("ecwmax "), NULL, 0);
		CONFIG_MSG("ac_val=%d, ecwmax=%d\n", ac_val, wme->ecwmax[ac_val]);
	}
	pick_tmp = pick;
	pch = bcmstrstr(pick_tmp, "txop ");
	if (pch) {
		wme->txop[ac_val] = (int)simple_strtol(pch+strlen("txop "), NULL, 0);
		CONFIG_MSG("ac_val=%d, txop=0x%x\n", ac_val, wme->txop[ac_val]);
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
		CONFIG_MSG("force_wme_ac = %d\n", conf->force_wme_ac);
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

#ifdef SET_FWNV_BY_MAC
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
		dhd_conf_free_mac_list(&conf->fw_by_mac);
		pick_tmp = data;
		pch = bcmstrtok(&pick_tmp, " ", 0);
		conf->fw_by_mac.count = (uint32)simple_strtol(pch, NULL, 0);
		if (!(mac_list = kmalloc(sizeof(wl_mac_list_t)*conf->fw_by_mac.count,
				GFP_KERNEL))) {
			conf->fw_by_mac.count = 0;
			CONFIG_ERROR("kmalloc failed\n");
		}
		CONFIG_MSG("fw_count=%d\n", conf->fw_by_mac.count);
		conf->fw_by_mac.m_mac_list_head = mac_list;
		for (i=0; i<conf->fw_by_mac.count; i++) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			strcpy(mac_list[i].name, pch);
			pch = bcmstrtok(&pick_tmp, " ", 0);
			mac_list[i].count = (uint32)simple_strtol(pch, NULL, 0);
			CONFIG_MSG("name=%s, mac_count=%d\n",
				mac_list[i].name, mac_list[i].count);
			if (!(mac_range = kmalloc(sizeof(wl_mac_range_t)*mac_list[i].count,
					GFP_KERNEL))) {
				mac_list[i].count = 0;
				CONFIG_ERROR("kmalloc failed\n");
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
				CONFIG_MSG("oui=0x%06X, nic_start=0x%06X, nic_end=0x%06X\n",
					mac_range[j].oui, mac_range[j].nic_start, mac_range[j].nic_end);
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
		dhd_conf_free_mac_list(&conf->nv_by_mac);
		pick_tmp = data;
		pch = bcmstrtok(&pick_tmp, " ", 0);
		conf->nv_by_mac.count = (uint32)simple_strtol(pch, NULL, 0);
		if (!(mac_list = kmalloc(sizeof(wl_mac_list_t)*conf->nv_by_mac.count,
				GFP_KERNEL))) {
			conf->nv_by_mac.count = 0;
			CONFIG_ERROR("kmalloc failed\n");
		}
		CONFIG_MSG("nv_count=%d\n", conf->nv_by_mac.count);
		conf->nv_by_mac.m_mac_list_head = mac_list;
		for (i=0; i<conf->nv_by_mac.count; i++) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			strcpy(mac_list[i].name, pch);
			pch = bcmstrtok(&pick_tmp, " ", 0);
			mac_list[i].count = (uint32)simple_strtol(pch, NULL, 0);
			CONFIG_MSG("name=%s, mac_count=%d\n",
				mac_list[i].name, mac_list[i].count);
			if (!(mac_range = kmalloc(sizeof(wl_mac_range_t)*mac_list[i].count,
					GFP_KERNEL))) {
				mac_list[i].count = 0;
				CONFIG_ERROR("kmalloc failed\n");
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
				CONFIG_MSG("oui=0x%06X, nic_start=0x%06X, nic_end=0x%06X\n",
					mac_range[j].oui, mac_range[j].nic_start, mac_range[j].nic_end);
			}
		}
	}
	else
		return false;

	return true;
}
#endif

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
		dhd_conf_free_chip_nv_path_list(&conf->nv_by_chip);
		pick_tmp = data;
		pch = bcmstrtok(&pick_tmp, " ", 0);
		conf->nv_by_chip.count = (uint32)simple_strtol(pch, NULL, 0);
		if (!(chip_nv_path = kmalloc(sizeof(wl_chip_nv_path_t)*conf->nv_by_chip.count,
				GFP_KERNEL))) {
			conf->nv_by_chip.count = 0;
			CONFIG_ERROR("kmalloc failed\n");
		}
		CONFIG_MSG("nv_by_chip_count=%d\n", conf->nv_by_chip.count);
		conf->nv_by_chip.m_chip_nv_path_head = chip_nv_path;
		for (i=0; i<conf->nv_by_chip.count; i++) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			chip_nv_path[i].chip = (uint32)simple_strtol(pch, NULL, 0);
			pch = bcmstrtok(&pick_tmp, " ", 0);
			chip_nv_path[i].chiprev = (uint32)simple_strtol(pch, NULL, 0);
			pch = bcmstrtok(&pick_tmp, " ", 0);
			strcpy(chip_nv_path[i].name, pch);
			CONFIG_MSG("chip=0x%x, chiprev=%d, name=%s\n",
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
		CONFIG_MSG("roam_off = %d\n", conf->roam_off);
	}
	else if (!strncmp("roam_off_suspend=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->roam_off_suspend = 0;
		else
			conf->roam_off_suspend = 1;
		CONFIG_MSG("roam_off_suspend = %d\n", conf->roam_off_suspend);
	}
	else if (!strncmp("roam_trigger=", full_param, len_param)) {
		conf->roam_trigger[0] = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("roam_trigger = %d\n", conf->roam_trigger[0]);
	}
	else if (!strncmp("roam_scan_period=", full_param, len_param)) {
		conf->roam_scan_period[0] = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("roam_scan_period = %d\n", conf->roam_scan_period[0]);
	}
	else if (!strncmp("roam_delta=", full_param, len_param)) {
		conf->roam_delta[0] = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("roam_delta = %d\n", conf->roam_delta[0]);
	}
	else if (!strncmp("fullroamperiod=", full_param, len_param)) {
		conf->fullroamperiod = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("fullroamperiod = %d\n", conf->fullroamperiod);
	} else
		return false;

	return true;
}

bool
dhd_conf_read_country(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	struct dhd_conf *conf = dhd->conf;
	country_list_t *country_next = NULL, *country;
	int i, count = 0;
	char *pch, *pick_tmp, *pick_tmp2;
	char *data = full_param+len_param;
	uint len_data = strlen(data);

	/* Process country_list:
	 * country_list=[country1]:[ccode1]/[regrev1],
	 * [country2]:[ccode2]/[regrev2] \
	 * Ex: country_list=US:US/0, TW:TW/1
	 */
	if (!strncmp("ccode=", full_param, len_param)) {
		len_data = min((uint)WLC_CNTRY_BUF_SZ, len_data);
		memset(&conf->cspec, 0, sizeof(wl_country_t));
		memcpy(conf->cspec.country_abbrev, data, len_data);
		memcpy(conf->cspec.ccode, data, len_data);
		CONFIG_MSG("ccode = %s\n", conf->cspec.ccode);
	}
	else if (!strncmp("regrev=", full_param, len_param)) {
		conf->cspec.rev = (int32)simple_strtol(data, NULL, 10);
		CONFIG_MSG("regrev = %d\n", conf->cspec.rev);
	}
	else if (!strncmp("country_list=", full_param, len_param)) {
		dhd_conf_free_country_list(conf);
		pick_tmp = data;
		for (i=0; i<CONFIG_COUNTRY_LIST_SIZE; i++) {
			pick_tmp2 = bcmstrtok(&pick_tmp, ", ", 0);
			if (!pick_tmp2)
				break;
			pch = bcmstrtok(&pick_tmp2, ":", 0);
			if (!pch)
				break;
			country = NULL;
			if (!(country = kmalloc(sizeof(country_list_t), GFP_KERNEL))) {
				CONFIG_ERROR("kmalloc failed\n");
				break;
			}
			memset(country, 0, sizeof(country_list_t));

			memcpy(country->cspec.country_abbrev, pch, 2);
			pch = bcmstrtok(&pick_tmp2, "/", 0);
			if (!pch) {
				kfree(country);
				break;
			}
			memcpy(country->cspec.ccode, pch, 2);
			pch = bcmstrtok(&pick_tmp2, "/", 0);
			if (!pch) {
				kfree(country);
				break;
			}
			country->cspec.rev = (int32)simple_strtol(pch, NULL, 10);
			count++;
			if (!conf->country_head) {
				conf->country_head = country;
				country_next = country;
			} else {
				country_next->next = country;
				country_next = country;
			}
			CONFIG_TRACE("abbrev=%s, ccode=%s, regrev=%d\n",
				country->cspec.country_abbrev, country->cspec.ccode, country->cspec.rev);
		}
		CONFIG_MSG("%d country in list\n", count);
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
	mchan_params_t *mchan_next = NULL, *mchan;
	char *data = full_param+len_param;

	/* Process mchan_bw:
	 * mchan_bw=[val]/[any/go/gc]/[any/source/sink]
	 * Ex: mchan_bw=80/go/source, 30/gc/sink
	 */
	if (!strncmp("mchan_bw=", full_param, len_param)) {
		dhd_conf_free_mchan_list(conf);
		pick_tmp = data;
		for (i=0; i<MCHAN_MAX_NUM; i++) {
			pick_tmp2 = bcmstrtok(&pick_tmp, ", ", 0);
			if (!pick_tmp2)
				break;
			pch = bcmstrtok(&pick_tmp2, "/", 0);
			if (!pch)
				break;

			mchan = NULL;
			if (!(mchan = kmalloc(sizeof(mchan_params_t), GFP_KERNEL))) {
				CONFIG_ERROR("kmalloc failed\n");
				break;
			}
			memset(mchan, 0, sizeof(mchan_params_t));

			mchan->bw = (int)simple_strtol(pch, NULL, 0);
			if (mchan->bw < 0 || mchan->bw > 100) {
				CONFIG_ERROR("wrong bw %d\n", mchan->bw);
				kfree(mchan);
				break;
			}

			pch = bcmstrtok(&pick_tmp2, "/", 0);
			if (!pch) {
				kfree(mchan);
				break;
			} else {
				if (bcmstrstr(pch, "any")) {
					mchan->p2p_mode = -1;
				} else if (bcmstrstr(pch, "go")) {
					mchan->p2p_mode = WL_P2P_IF_GO;
				} else if (bcmstrstr(pch, "gc")) {
					mchan->p2p_mode = WL_P2P_IF_CLIENT;
				}
			}
			pch = bcmstrtok(&pick_tmp2, "/", 0);
			if (!pch) {
				kfree(mchan);
				break;
			} else {
				if (bcmstrstr(pch, "any")) {
					mchan->miracast_mode = -1;
				} else if (bcmstrstr(pch, "source")) {
					mchan->miracast_mode = MIRACAST_SOURCE;
				} else if (bcmstrstr(pch, "sink")) {
					mchan->miracast_mode = MIRACAST_SINK;
				}
			}
			if (!conf->mchan) {
				conf->mchan = mchan;
				mchan_next = mchan;
			} else {
				mchan_next->next = mchan;
				mchan_next = mchan;
			}
			CONFIG_TRACE("mchan_bw=%d/%d/%d\n", mchan->bw,mchan->p2p_mode,
				mchan->miracast_mode);
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
	 * 2) pkt_filter_delete=100, 102, 103, 104, 105
	 * 3) magic_pkt_filter_add=141 0 1 12
	 */
	if (!strncmp("dhd_master_mode=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			dhd_master_mode = FALSE;
		else
			dhd_master_mode = TRUE;
		CONFIG_MSG("dhd_master_mode = %d\n", dhd_master_mode);
	}
	else if (!strncmp("pkt_filter_add=", full_param, len_param)) {
		pick_tmp = data;
		pch = bcmstrtok(&pick_tmp, ",.-", 0);
		i=0;
		while (pch != NULL && i<DHD_CONF_FILTER_MAX) {
			strcpy(&conf->pkt_filter_add.filter[i][0], pch);
			CONFIG_MSG("pkt_filter_add[%d][] = %s\n",
				i, &conf->pkt_filter_add.filter[i][0]);
			pch = bcmstrtok(&pick_tmp, ",.-", 0);
			i++;
		}
		conf->pkt_filter_add.count = i;
	}
	else if (!strncmp("pkt_filter_delete=", full_param, len_param) ||
			!strncmp("pkt_filter_del=", full_param, len_param)) {
		pick_tmp = data;
		pch = bcmstrtok(&pick_tmp, " ,.-", 0);
		i=0;
		while (pch != NULL && i<DHD_CONF_FILTER_MAX) {
			conf->pkt_filter_del.id[i] = (uint32)simple_strtol(pch, NULL, 10);
			pch = bcmstrtok(&pick_tmp, " ,.-", 0);
			i++;
		}
		conf->pkt_filter_del.count = i;
		CONFIG_MSG("pkt_filter_del id = ");
		for (i=0; i<conf->pkt_filter_del.count; i++)
			printk(KERN_CONT "%d ", conf->pkt_filter_del.id[i]);
		printk(KERN_CONT "\n");
	}
	else if (!strncmp("magic_pkt_filter_add=", full_param, len_param)) {
		if (conf->magic_pkt_filter_add) {
			kfree(conf->magic_pkt_filter_add);
			conf->magic_pkt_filter_add = NULL;
		}
		if (!(conf->magic_pkt_filter_add = kmalloc(MAGIC_PKT_FILTER_LEN, GFP_KERNEL))) {
			CONFIG_ERROR("kmalloc failed\n");
		} else {
			memset(conf->magic_pkt_filter_add, 0, MAGIC_PKT_FILTER_LEN);
			strcpy(conf->magic_pkt_filter_add, data);
			CONFIG_MSG("magic_pkt_filter_add = %s\n", conf->magic_pkt_filter_add);
		}
	}
	else
		return false;

	return true;
}
#endif

#ifdef ISAM_PREINIT
#if !defined(WL_EXT_IAPSTA)
#error "WL_EXT_IAPSTA should be defined to enable ISAM_PREINIT"
#endif /* !WL_EXT_IAPSTA */
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
		CONFIG_MSG("isam_init=%s\n", conf->isam_init);
	}
	else if (!strncmp("isam_config=", full_param, len_param)) {
		sprintf(conf->isam_config, "isam_config %s", data);
		CONFIG_MSG("isam_config=%s\n", conf->isam_config);
	}
	else if (!strncmp("isam_enable=", full_param, len_param)) {
		sprintf(conf->isam_enable, "isam_enable %s", data);
		CONFIG_MSG("isam_enable=%s\n", conf->isam_enable);
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
		CONFIG_MSG("dhcpc_enable = %d\n", conf->dhcpc_enable);
	}
	else if (!strncmp("dhcpd_enable=", full_param, len_param)) {
		conf->dhcpd_enable = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("dhcpd_enable = %d\n", conf->dhcpd_enable);
	}
	else if (!strncmp("dhcpd_ip_addr=", full_param, len_param)) {
		if (!bcm_atoipv4(data, &ipa_set)) {
			CONFIG_ERROR("dhcpd_ip_addr adress setting failed.n");
			return false;
		}
		memcpy(&conf->dhcpd_ip_addr, &ipa_set, sizeof(struct ipv4_addr));
		CONFIG_MSG("dhcpd_ip_addr = %s\n", data);
	}
	else if (!strncmp("dhcpd_ip_mask=", full_param, len_param)) {
		if (!bcm_atoipv4(data, &ipa_set)) {
			CONFIG_ERROR("dhcpd_ip_mask adress setting failed\n");
			return false;
		}
		memcpy(&conf->dhcpd_ip_mask, &ipa_set, sizeof(struct ipv4_addr));
		CONFIG_MSG("dhcpd_ip_mask = %s\n", data);
	}
	else if (!strncmp("dhcpd_ip_start=", full_param, len_param)) {
		if (!bcm_atoipv4(data, &ipa_set)) {
			CONFIG_ERROR("dhcpd_ip_start adress setting failed\n");
			return false;
		}
		memcpy(&conf->dhcpd_ip_start, &ipa_set, sizeof(struct ipv4_addr));
		CONFIG_MSG("dhcpd_ip_start = %s\n", data);
	}
	else if (!strncmp("dhcpd_ip_end=", full_param, len_param)) {
		if (!bcm_atoipv4(data, &ipa_set)) {
			CONFIG_ERROR("dhcpd_ip_end adress setting failed\n");
			return false;
		}
		memcpy(&conf->dhcpd_ip_end, &ipa_set, sizeof(struct ipv4_addr));
		CONFIG_MSG("dhcpd_ip_end = %s\n", data);
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
		CONFIG_MSG("dhd_doflow = %d\n", dhd_doflow);
	}
	else if (!strncmp("dhd_slpauto=", full_param, len_param) ||
			!strncmp("kso_enable=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			dhd_slpauto = FALSE;
		else
			dhd_slpauto = TRUE;
		CONFIG_MSG("dhd_slpauto = %d\n", dhd_slpauto);
	}
	else if (!strncmp("use_rxchain=", full_param, len_param)) {
		conf->use_rxchain = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("use_rxchain = %d\n", conf->use_rxchain);
	}
	else if (!strncmp("dhd_txminmax=", full_param, len_param)) {
		conf->dhd_txminmax = (uint)simple_strtol(data, NULL, 10);
		CONFIG_MSG("dhd_txminmax = %d\n", conf->dhd_txminmax);
	}
	else if (!strncmp("txinrx_thres=", full_param, len_param)) {
		conf->txinrx_thres = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("txinrx_thres = %d\n", conf->txinrx_thres);
	}
#ifdef DYNAMIC_MAX_HDR_READ
	else if (!strncmp("max_hdr_read=", full_param, len_param)) {
		conf->max_hdr_read = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("max_hdr_read = %d\n", conf->max_hdr_read);
	}
	else if (!strncmp("dhd_firstread=", full_param, len_param)) {
		firstread = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("dhd_firstread = %d\n", firstread);
	}
#endif
#if defined(HW_OOB)
	else if (!strncmp("oob_enabled_later=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->oob_enabled_later = FALSE;
		else
			conf->oob_enabled_later = TRUE;
		CONFIG_MSG("oob_enabled_later = %d\n", conf->oob_enabled_later);
	}
#endif
	else if (!strncmp("dpc_cpucore=", full_param, len_param)) {
		conf->dpc_cpucore = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("dpc_cpucore = %d\n", conf->dpc_cpucore);
	}
	else if (!strncmp("rxf_cpucore=", full_param, len_param)) {
		conf->rxf_cpucore = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("rxf_cpucore = %d\n", conf->rxf_cpucore);
	}
	else if (!strncmp("dhd_dpc_prio=", full_param, len_param)) {
		conf->dhd_dpc_prio = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("dhd_dpc_prio = %d\n", conf->dhd_dpc_prio);
	}
#if defined(BCMSDIOH_TXGLOM)
	else if (!strncmp("txglomsize=", full_param, len_param)) {
		conf->txglomsize = (uint)simple_strtol(data, NULL, 10);
		if (conf->txglomsize > SDPCM_MAXGLOM_SIZE)
			conf->txglomsize = SDPCM_MAXGLOM_SIZE;
		CONFIG_MSG("txglomsize = %d\n", conf->txglomsize);
	}
	else if (!strncmp("txglom_ext=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->txglom_ext = FALSE;
		else
			conf->txglom_ext = TRUE;
		CONFIG_MSG("txglom_ext = %d\n", conf->txglom_ext);
		if (conf->txglom_ext) {
			if ((conf->chip == BCM43362_CHIP_ID) || (conf->chip == BCM4330_CHIP_ID))
				conf->txglom_bucket_size = 1680;
			else if (conf->chip == BCM43340_CHIP_ID || conf->chip == BCM43341_CHIP_ID ||
					conf->chip == BCM4334_CHIP_ID || conf->chip == BCM4324_CHIP_ID)
				conf->txglom_bucket_size = 1684;
		}
		CONFIG_MSG("txglom_bucket_size = %d\n", conf->txglom_bucket_size);
	}
	else if (!strncmp("bus:rxglom=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->bus_rxglom = FALSE;
		else
			conf->bus_rxglom = TRUE;
		CONFIG_MSG("bus:rxglom = %d\n", conf->bus_rxglom);
	}
	else if (!strncmp("deferred_tx_len=", full_param, len_param)) {
		conf->deferred_tx_len = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("deferred_tx_len = %d\n", conf->deferred_tx_len);
	}
	else if (!strncmp("txctl_tmo_fix=", full_param, len_param)) {
		conf->txctl_tmo_fix = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("txctl_tmo_fix = %d\n", conf->txctl_tmo_fix);
	}
	else if (!strncmp("tx_max_offset=", full_param, len_param)) {
		conf->tx_max_offset = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("tx_max_offset = %d\n", conf->tx_max_offset);
	}
	else if (!strncmp("txglom_mode=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->txglom_mode = FALSE;
		else
			conf->txglom_mode = TRUE;
		CONFIG_MSG("txglom_mode = %d\n", conf->txglom_mode);
	}
#if defined(SDIO_ISR_THREAD)
	else if (!strncmp("intr_extn=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->intr_extn = FALSE;
		else
			conf->intr_extn = TRUE;
		CONFIG_MSG("intr_extn = %d\n", conf->intr_extn);
	}
#endif
#ifdef BCMSDIO_RXLIM_POST
	else if (!strncmp("rxlim_en=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->rxlim_en = FALSE;
		else
			conf->rxlim_en = TRUE;
		CONFIG_MSG("rxlim_en = %d\n", conf->rxlim_en);
	}
#endif
#ifdef BCMSDIO_TXSEQ_SYNC
	else if (!strncmp("txseq_sync=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->txseq_sync = FALSE;
		else
			conf->txseq_sync = TRUE;
		CONFIG_MSG("txseq_sync = %d\n", conf->txseq_sync);
	}
#endif
#endif
#ifdef MINIME
	else if (!strncmp("ramsize=", full_param, len_param)) {
		conf->ramsize = (uint32)simple_strtol(data, NULL, 0);
		CONFIG_MSG("ramsize = %d\n", conf->ramsize);
	}
#endif
#ifdef BCMSDIO_INTSTATUS_WAR
	else if (!strncmp("read_intr_mode=", full_param, len_param)) {
		conf->read_intr_mode = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("read_intr_mode = %d\n", conf->read_intr_mode);
	}
#endif
	else if (!strncmp("kso_try_max=", full_param, len_param)) {
		conf->kso_try_max = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("kso_try_max = %d\n", conf->kso_try_max);
	}
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
		CONFIG_MSG("bus:deepsleep_disable = %d\n", conf->bus_deepsleep_disable);
	}
	else if (!strncmp("flow_ring_queue_threshold=", full_param, len_param)) {
		conf->flow_ring_queue_threshold = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("flow_ring_queue_threshold = %d\n", conf->flow_ring_queue_threshold);
	}
	else if (!strncmp("d2h_intr_control=", full_param, len_param)) {
		conf->d2h_intr_control = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("d2h_intr_control = %d\n", conf->d2h_intr_control);
	}
	else if (!strncmp("enq_hdr_pkt=", full_param, len_param)) {
		conf->enq_hdr_pkt = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("enq_hdr_pkt = 0x%x\n", conf->enq_hdr_pkt);
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
		CONFIG_MSG("deepsleep = %d\n", conf->deepsleep);
	}
	else if (!strncmp("PM=", full_param, len_param)) {
		conf->pm = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("PM = %d\n", conf->pm);
	}
	else if (!strncmp("pm_in_suspend=", full_param, len_param)) {
		conf->pm_in_suspend = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("pm_in_suspend = %d\n", conf->pm_in_suspend);
	}
	else if (!strncmp("suspend_mode=", full_param, len_param)) {
		conf->suspend_mode = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("suspend_mode = %d\n", conf->suspend_mode);
		if (conf->suspend_mode == EARLY_SUSPEND)
			conf->insuspend &= ~(NO_TXDATA_IN_SUSPEND | NO_TXCTL_IN_SUSPEND);
		else if (conf->suspend_mode == PM_NOTIFIER ||
				conf->suspend_mode == SUSPEND_MODE_2)
			conf->insuspend |= (NO_TXDATA_IN_SUSPEND | NO_TXCTL_IN_SUSPEND);
		CONFIG_MSG("insuspend = 0x%x\n", conf->insuspend);
	}
	else if (!strncmp("suspend_bcn_li_dtim=", full_param, len_param)) {
		conf->suspend_bcn_li_dtim = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("suspend_bcn_li_dtim = %d\n", conf->suspend_bcn_li_dtim);
	}
	else if (!strncmp("xmit_in_suspend=", full_param, len_param)) {
		if (!strncmp(data, "1", 1))
			conf->insuspend &= ~NO_TXDATA_IN_SUSPEND;
		else
			conf->insuspend |= NO_TXDATA_IN_SUSPEND;
		CONFIG_MSG("insuspend = 0x%x\n", conf->insuspend);
	}
	else if (!strncmp("insuspend=", full_param, len_param)) {
		conf->insuspend = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("insuspend = 0x%x\n", conf->insuspend);
	}
#ifdef WL_EXT_WOWL
	else if (!strncmp("wowl=", full_param, len_param)) {
		conf->wowl = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("wowl = 0x%x\n", conf->wowl);
	}
#endif
	else if (!strncmp("rekey_offload=", full_param, len_param)) {
		if (!strncmp(data, "1", 1))
			conf->rekey_offload = TRUE;
		else
			conf->rekey_offload = FALSE;
		CONFIG_MSG("rekey_offload = %d\n", conf->rekey_offload);
	}
	else
		return false;

	return true;
}

#ifdef GET_CUSTOM_MAC_FROM_CONFIG
int
bcm_str2hex(const char *p, char *ea, int size)
{
	int i = 0;
	char *ep;

	for (;;) {
		ea[i++] = (char) bcm_strtoul(p, &ep, 16);
		p = ep;
		if (!*p++ || i == size)
			break;
	}

	return (i == size);
}
#endif

bool
dhd_conf_read_others(dhd_pub_t *dhd, char *full_param, uint len_param)
{
	struct dhd_conf *conf = dhd->conf;
	char *data = full_param+len_param;
	char *pch, *pick_tmp;
	int i;
#ifdef GET_CUSTOM_MAC_FROM_CONFIG
	struct ether_addr ea_addr;
	char macpad[56];
#endif

	if (!strncmp("dhd_poll=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->dhd_poll = 0;
		else
			conf->dhd_poll = 1;
		CONFIG_MSG("dhd_poll = %d\n", conf->dhd_poll);
	}
	else if (!strncmp("dhd_watchdog_ms=", full_param, len_param)) {
		dhd_watchdog_ms = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("dhd_watchdog_ms = %d\n", dhd_watchdog_ms);
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
		CONFIG_MSG("band = %d\n", conf->band);
	}
	else if (!strncmp("bw_cap_2g=", full_param, len_param)) {
		conf->bw_cap[0] = (uint)simple_strtol(data, NULL, 0);
		CONFIG_MSG("bw_cap_2g = %d\n", conf->bw_cap[0]);
	}
	else if (!strncmp("bw_cap_5g=", full_param, len_param)) {
		conf->bw_cap[1] = (uint)simple_strtol(data, NULL, 0);
		CONFIG_MSG("bw_cap_5g = %d\n", conf->bw_cap[1]);
	}
	else if (!strncmp("bw_cap=", full_param, len_param)) {
		pick_tmp = data;
		pch = bcmstrtok(&pick_tmp, " ,.-", 0);
		if (pch != NULL) {
			conf->bw_cap[0] = (uint32)simple_strtol(pch, NULL, 0);
			CONFIG_MSG("bw_cap 2g = %d\n", conf->bw_cap[0]);
		}
		pch = bcmstrtok(&pick_tmp, " ,.-", 0);
		if (pch != NULL) {
			conf->bw_cap[1] = (uint32)simple_strtol(pch, NULL, 0);
			CONFIG_MSG("bw_cap 5g = %d\n", conf->bw_cap[1]);
		}
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
		CONFIG_MSG("channels = ");
		for (i=0; i<conf->channels.count; i++)
			printk(KERN_CONT "%d ", conf->channels.channel[i]);
		printk(KERN_CONT "\n");
	}
	else if (!strncmp("keep_alive_period=", full_param, len_param)) {
		conf->keep_alive_period = (uint)simple_strtol(data, NULL, 10);
		CONFIG_MSG("keep_alive_period = %d\n", conf->keep_alive_period);
	}
#ifdef ARP_OFFLOAD_SUPPORT
	else if (!strncmp("garp=", full_param, len_param)) {
		if (!strncmp(data, "0", 1))
			conf->garp = FALSE;
		else
			conf->garp = TRUE;
		CONFIG_MSG("garp = %d\n", conf->garp);
	}
#endif
	else if (!strncmp("srl=", full_param, len_param)) {
		conf->srl = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("srl = %d\n", conf->srl);
	}
	else if (!strncmp("lrl=", full_param, len_param)) {
		conf->lrl = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("lrl = %d\n", conf->lrl);
	}
	else if (!strncmp("bcn_timeout=", full_param, len_param)) {
		conf->bcn_timeout= (uint)simple_strtol(data, NULL, 10);
		CONFIG_MSG("bcn_timeout = %d\n", conf->bcn_timeout);
	}
	else if (!strncmp("frameburst=", full_param, len_param)) {
		conf->frameburst = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("frameburst = %d\n", conf->frameburst);
	}
	else if (!strncmp("disable_proptx=", full_param, len_param)) {
		conf->disable_proptx = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("disable_proptx = %d\n", conf->disable_proptx);
	}
#ifdef DHDTCPACK_SUPPRESS
	else if (!strncmp("tcpack_sup_mode=", full_param, len_param)) {
		conf->tcpack_sup_mode = (uint)simple_strtol(data, NULL, 10);
		CONFIG_MSG("tcpack_sup_mode = %d\n", conf->tcpack_sup_mode);
	}
	else if (!strncmp("tcpack_sup_ratio=", full_param, len_param)) {
		conf->tcpack_sup_ratio = (uint)simple_strtol(data, NULL, 10);
		CONFIG_MSG("tcpack_sup_ratio = %d\n", conf->tcpack_sup_ratio);
	}
	else if (!strncmp("tcpack_sup_delay=", full_param, len_param)) {
		conf->tcpack_sup_delay = (uint)simple_strtol(data, NULL, 10);
		CONFIG_MSG("tcpack_sup_delay = %d\n", conf->tcpack_sup_delay);
	}
#endif
	else if (!strncmp("pktprio8021x=", full_param, len_param)) {
		conf->pktprio8021x = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("pktprio8021x = %d\n", conf->pktprio8021x);
	}
#if defined(BCMSDIO) || defined(BCMPCIE)
	else if (!strncmp("dhd_txbound=", full_param, len_param)) {
		dhd_txbound = (uint)simple_strtol(data, NULL, 10);
		CONFIG_MSG("dhd_txbound = %d\n", dhd_txbound);
	}
	else if (!strncmp("dhd_rxbound=", full_param, len_param)) {
		dhd_rxbound = (uint)simple_strtol(data, NULL, 10);
		CONFIG_MSG("dhd_rxbound = %d\n", dhd_rxbound);
	}
#endif
	else if (!strncmp("orphan_move=", full_param, len_param)) {
		conf->orphan_move = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("orphan_move = %d\n", conf->orphan_move);
	}
	else if (!strncmp("tsq=", full_param, len_param)) {
		conf->tsq = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("tsq = %d\n", conf->tsq);
	}
	else if (!strncmp("ctrl_resched=", full_param, len_param)) {
		conf->ctrl_resched = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("ctrl_resched = %d\n", conf->ctrl_resched);
	}
	else if (!strncmp("rxcnt_timeout=", full_param, len_param)) {
		conf->rxcnt_timeout = (int)simple_strtol(data, NULL, 10);
		CONFIG_MSG("rxcnt_timeout = %d\n", conf->rxcnt_timeout);
	}
	else if (!strncmp("in4way=", full_param, len_param)) {
		conf->in4way = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("in4way = 0x%x\n", conf->in4way);
	}
	else if (!strncmp("war=", full_param, len_param)) {
		conf->war = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("war = 0x%x\n", conf->war);
	}
	else if (!strncmp("wl_preinit=", full_param, len_param)) {
		if (conf->wl_preinit) {
			kfree(conf->wl_preinit);
			conf->wl_preinit = NULL;
		}
		if (!(conf->wl_preinit = kmalloc(strlen(data)+1, GFP_KERNEL))) {
			CONFIG_ERROR("kmalloc failed\n");
		} else {
			memset(conf->wl_preinit, 0, strlen(data)+1);
			strcpy(conf->wl_preinit, data);
			CONFIG_MSG("wl_preinit = %s\n", conf->wl_preinit);
		}
	}
	else if (!strncmp("wl_suspend=", full_param, len_param)) {
		if (conf->wl_suspend) {
			kfree(conf->wl_suspend);
			conf->wl_suspend = NULL;
		}
		if (!(conf->wl_suspend = kmalloc(strlen(data)+1, GFP_KERNEL))) {
			CONFIG_ERROR("kmalloc failed\n");
		} else {
			memset(conf->wl_suspend, 0, strlen(data)+1);
			strcpy(conf->wl_suspend, data);
			CONFIG_MSG("wl_suspend = %s\n", conf->wl_suspend);
		}
	}
	else if (!strncmp("wl_resume=", full_param, len_param)) {
		if (conf->wl_resume) {
			kfree(conf->wl_resume);
			conf->wl_resume = NULL;
		}
		if (!(conf->wl_resume = kmalloc(strlen(data)+1, GFP_KERNEL))) {
			CONFIG_ERROR("kmalloc failed\n");
		} else {
			memset(conf->wl_resume, 0, strlen(data)+1);
			strcpy(conf->wl_resume, data);
			CONFIG_MSG("wl_resume = %s\n", conf->wl_resume);
		}
	}
#ifdef GET_CUSTOM_MAC_FROM_CONFIG
	else if (!strncmp("mac=", full_param, len_param)) {
		if (!bcm_ether_atoe(data, &ea_addr)) {
			CONFIG_ERROR("mac adress read error");
			return false;
		}
		memcpy(&conf->hw_ether, &ea_addr, ETHER_ADDR_LEN);
		CONFIG_MSG("mac = %s\n", data);
	}
	else if (!strncmp("macpad=", full_param, len_param)) {
		if (!bcm_str2hex(data, macpad, sizeof(macpad))) {
			CONFIG_ERROR("macpad adress read error");
			return false;
		}
		memcpy(&conf->hw_ether[ETHER_ADDR_LEN], macpad, sizeof(macpad));
		if (config_msg_level & CONFIG_TRACE_LEVEL) {
			CONFIG_MSG("macpad =\n");
			for (i=0; i<sizeof(macpad); i++) {
				printk(KERN_CONT "0x%02x, ", conf->hw_ether[ETHER_ADDR_LEN+i]);
				if ((i+1)%8 == 0)
					printk(KERN_CONT "\n");
			}
		}
	}
#endif
#ifdef PROPTX_MAXCOUNT
	else if (!strncmp("proptx_maxcnt_2g=", full_param, len_param)) {
		conf->proptx_maxcnt_2g = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("proptx_maxcnt_2g = %d\n", conf->proptx_maxcnt_2g);
	}
	else if (!strncmp("proptx_maxcnt_5g=", full_param, len_param)) {
		conf->proptx_maxcnt_5g = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("proptx_maxcnt_5g = %d\n", conf->proptx_maxcnt_5g);
	}
#endif
#ifdef TPUT_MONITOR
	else if (!strncmp("data_drop_mode=", full_param, len_param)) {
		conf->data_drop_mode = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("data_drop_mode = %d\n", conf->data_drop_mode);
	}
	else if (!strncmp("tput_monitor_ms=", full_param, len_param)) {
		conf->tput_monitor_ms = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("tput_monitor_ms = %d\n", conf->tput_monitor_ms);
	}
#ifdef BCMSDIO
	else if (!strncmp("doflow_tput_thresh=", full_param, len_param)) {
		conf->doflow_tput_thresh = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("doflow_tput_thresh = %d\n", conf->doflow_tput_thresh);
		if (conf->doflow_tput_thresh > 0)
			conf->tput_monitor_ms = 1000;
	}
#endif
#endif
#ifdef SCAN_SUPPRESS
	else if (!strncmp("scan_intput=", full_param, len_param)) {
		conf->scan_intput = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("scan_intput = 0x%x\n", conf->scan_intput);
	}
	else if (!strncmp("scan_tput_thresh=", full_param, len_param)) {
		conf->scan_tput_thresh = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("scan_tput_thresh = %d\n", conf->scan_tput_thresh);
		if (conf->scan_tput_thresh > 0)
			conf->tput_monitor_ms = 1000;
	}
	else if (!strncmp("scan_busy_tmo=", full_param, len_param)) {
		conf->scan_busy_tmo = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("scan_busy_tmo = %d\n", conf->scan_busy_tmo);
	}
	else if (!strncmp("scan_busy_thresh=", full_param, len_param)) {
		conf->scan_busy_thresh = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("scan_busy_thresh = %d\n", conf->scan_busy_thresh);
	}
#endif
#ifdef DHD_TPUT_PATCH
	else if (!strncmp("tput_patch=", full_param, len_param)) {
		if (!strncmp(data, "1", 1))
			conf->tput_patch = TRUE;
		else
			conf->tput_patch = FALSE;
		CONFIG_MSG("tput_patch = %d\n", conf->tput_patch);
		dhd_conf_set_tput_patch(dhd);
	}
	else if (!strncmp("mtu=", full_param, len_param)) {
		conf->mtu = (int)simple_strtol(data, NULL, 0);
		CONFIG_MSG("mtu = %d\n", conf->mtu);
	}
	else if (!strncmp("pktsetsum=", full_param, len_param)) {
		if (!strncmp(data, "1", 1))
			conf->pktsetsum = TRUE;
		else
			conf->pktsetsum = FALSE;
		CONFIG_MSG("pktsetsum = %d\n", conf->pktsetsum);
	}
#endif
#ifdef SET_XPS_CPUS
	else if (!strncmp("xps_cpus=", full_param, len_param)) {
		if (!strncmp(data, "1", 1))
			conf->xps_cpus = TRUE;
		else
			conf->xps_cpus = FALSE;
		CONFIG_MSG("xps_cpus = %d\n", conf->xps_cpus);
	}
#endif
#ifdef SET_RPS_CPUS
	else if (!strncmp("rps_cpus=", full_param, len_param)) {
		if (!strncmp(data, "1", 1))
			conf->rps_cpus = TRUE;
		else
			conf->rps_cpus = FALSE;
		CONFIG_MSG("rps_cpus = %d\n", conf->rps_cpus);
	}
#endif
#ifdef CHECK_DOWNLOAD_FW
	else if (!strncmp("fwchk=", full_param, len_param)) {
		if (!strncmp(data, "1", 1))
			conf->fwchk = TRUE;
		else
			conf->fwchk = FALSE;
		CONFIG_MSG("fwchk = %d\n", conf->fwchk);
	}
#endif
	else if (!strncmp("vndr_ie_assocreq=", full_param, len_param)) {
		if (conf->vndr_ie_assocreq) {
			kfree(conf->vndr_ie_assocreq);
			conf->vndr_ie_assocreq = NULL;
		}
		if (!(conf->vndr_ie_assocreq = kmalloc(strlen(data)+1, GFP_KERNEL))) {
			CONFIG_ERROR("kmalloc failed\n");
		} else {
			memset(conf->vndr_ie_assocreq, 0, strlen(data)+1);
			strcpy(conf->vndr_ie_assocreq, data);
			CONFIG_MSG("vndr_ie_assocreq = %s\n", conf->vndr_ie_assocreq);
		}
	}
	else
		return false;

	return true;
}

int
dhd_conf_read_config(dhd_pub_t *dhd, char *conf_path)
{
	int bcmerror = -1, chip_match = -1;
	uint len = 0, memblock_len = 0, start_pos=0, end_pos=0;
	char *memblock = NULL;
	char *bufp, *pick = NULL, *pch;
	bool conf_file_exists;
	uint len_param;

	len = MAXSZ_CONFIG;

	conf_file_exists = ((conf_path != NULL) && (conf_path[0] != '\0'));
	if (!conf_file_exists) {
		CONFIG_MSG("config path %s\n", conf_path);
		return (0);
	}

	if (conf_file_exists)
		bcmerror = dhd_get_download_buffer(dhd, conf_path, NVRAM, &memblock,
			(int *)&len);
	else
		bcmerror = dhd_get_download_buffer(dhd, NULL, NVRAM, &memblock, (int *)&len);

	if (bcmerror != BCME_OK) {
		CONFIG_MSG("Ignore config file %s\n", conf_path);
		goto err;
	}

#ifdef DHD_LINUX_STD_FW_API
	memblock_len = len;
#else
	memblock_len = MAXSZ_CONFIG;
#endif /* DHD_LINUX_STD_FW_API */

	pick = MALLOC(dhd->osh, MAXSZ_BUF);
	if (!pick) {
		CONFIG_ERROR("Failed to allocate memory %d bytes\n", MAXSZ_BUF);
		goto err;
	}

	/* Read variables */
	if (len > 0 && len < MAXSZ_CONFIG) {
		bufp = (char *)memblock;
		bufp[len] = 0;

		while (start_pos < len) {
			memset(pick, 0, MAXSZ_BUF);
			end_pos = pick_config_vars(bufp, len, start_pos, pick, MAXSZ_BUF);
			if (end_pos - start_pos >= MAXSZ_BUF)
				CONFIG_ERROR("out of buf to read MAXSIZ_BUF=%d\n", MAXSZ_BUF);
			start_pos = end_pos;
			pch = strchr(pick, '=');
			if (pch != NULL) {
				len_param = pch-pick+1;
				if (len_param == strlen(pick)) {
					CONFIG_ERROR("not a right parameter %s\n", pick);
					continue;
				}
			} else {
				CONFIG_ERROR("not a right parameter %s\n", pick);
				continue;
			}

			dhd_conf_read_chiprev(dhd, &chip_match, pick, len_param);
			if (!chip_match)
				continue;

			if (dhd_conf_read_log_level(dhd, pick, len_param))
				continue;
			else if (dhd_conf_read_roam_params(dhd, pick, len_param))
				continue;
			else if (dhd_conf_read_wme_ac_params(dhd, pick, len_param))
				continue;
#ifdef SET_FWNV_BY_MAC
			else if (dhd_conf_read_fw_by_mac(dhd, pick, len_param))
				continue;
			else if (dhd_conf_read_nv_by_mac(dhd, pick, len_param))
				continue;
#endif
			else if (dhd_conf_read_nv_by_chip(dhd, pick, len_param))
				continue;
			else if (dhd_conf_read_country(dhd, pick, len_param))
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
		CONFIG_ERROR("error reading config file: %d\n", len);
		bcmerror = BCME_SDIO_ERROR;
	}

err:
	if (pick)
		MFREE(dhd->osh, pick, MAXSZ_BUF);

	if (memblock)
		dhd_free_download_buffer(dhd, memblock, memblock_len);

	return bcmerror;
}

#if defined(BCMSDIO) || defined(BCMPCIE)
void
dhd_conf_set_devid(dhd_pub_t *dhd)
{
	wifi_adapter_info_t *adapter = NULL;
	uint32 bus_type = -1;
	uint32 bus_num = -1;
	uint32 slot_num = -1;

	dhd_bus_get_ids(dhd->bus, &bus_type, &bus_num, &slot_num);
	adapter = dhd_wifi_platform_get_adapter(bus_type, bus_num, slot_num);
	if (adapter) {
#if defined(BCMSDIO)
		dhd->conf->devid = adapter->sdio_func->device;
#endif
#if defined(BCMPCIE)
		dhd->conf->devid = adapter->pci_dev->device;
		dhd->conf->svid = adapter->pci_dev->subsystem_vendor;
		dhd->conf->ssid = adapter->pci_dev->subsystem_device;
#endif
	} else {
		CONFIG_ERROR("can't find adapter\n");
	}

	return;
}
#endif

int
dhd_conf_set_chiprev(dhd_pub_t *dhd, uint chip, uint chiprev)
{
#if defined(BCMSDIO) || defined(BCMPCIE)
	dhd_conf_set_devid(dhd);
#endif
	dhd->conf->chip = chip;
	dhd->conf->chiprev = chiprev;

#if defined(BCMSDIO)
	CONFIG_MSG("devid=0x%x, chip=0x%x, chiprev=%d\n",
		dhd->conf->devid, dhd->conf->chip, dhd->conf->chiprev);
#endif
#if defined(BCMPCIE)
	CONFIG_MSG("devid=0x%x, chip=0x%x, chiprev=%d, svid=0x%04x, ssid=0x%04x\n",
		dhd->conf->devid, dhd->conf->chip, dhd->conf->chiprev,
		dhd->conf->svid, dhd->conf->ssid);
#endif
#if defined(BCMDBUS)
	CONFIG_MSG("chip=0x%x, chiprev=%d\n", dhd->conf->chip, dhd->conf->chiprev);
#endif

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
		if (conf->txglom_ext)
			CONFIG_MSG("txglom_ext=%d, txglom_bucket_size=%d\n",
				conf->txglom_ext, conf->txglom_bucket_size);
		CONFIG_MSG("txglom_mode=%s\n",
			conf->txglom_mode==SDPCM_TXGLOM_MDESC?"multi-desc":"copy");
		CONFIG_MSG("txglomsize=%d, deferred_tx_len=%d\n",
			conf->txglomsize, conf->deferred_tx_len);
		CONFIG_MSG("txinrx_thres=%d, dhd_txminmax=%d\n",
			conf->txinrx_thres, conf->dhd_txminmax);
		CONFIG_MSG("tx_max_offset=%d, txctl_tmo_fix=%d\n",
			conf->tx_max_offset, conf->txctl_tmo_fix);
	} else {
		// clear txglom parameters
		conf->txglom_ext = FALSE;
		conf->txglom_bucket_size = 0;
		conf->txglomsize = 0;
		conf->deferred_tx_len = 0;
	}

}
#endif

#ifdef UPDATE_MODULE_NAME
#if defined(BCMSDIO) || defined(BCMPCIE)
static void
dhd_conf_compat_vht(dhd_pub_t *dhd)
{
	char vht_features[] = "vht_features=0";

	CONFIG_TRACE("Enter\n");

	dhd_conf_set_wl_cmd(dhd, vht_features, TRUE);
}
#endif

int
dhd_conf_compat_func(dhd_pub_t *dhd)
{
	const module_name_map_t* row = NULL;

	row = dhd_conf_match_module(dhd);
	if (row && row->compat_func) {
		row->compat_func(dhd);
	}

	return 0;
}
#endif

void
dhd_conf_postinit_ioctls(dhd_pub_t *dhd)
{
	struct dhd_conf *conf = dhd->conf;
	char wl_preinit[] = "assoc_retry_max=10";
#ifdef NO_POWER_SAVE
	char wl_no_power_save[] = "mpc=0, 86=0";
	dhd_conf_set_wl_cmd(dhd, wl_no_power_save, FALSE);
#endif

	dhd_conf_set_intiovar(dhd, 0, WLC_UP, "WLC_UP", 0, 0, FALSE);
	dhd_conf_map_country_list(dhd, &conf->cspec);
	dhd_conf_set_country(dhd, &conf->cspec);
	dhd_conf_fix_country(dhd);
	dhd_conf_get_country(dhd, &dhd->dhd_cspec);

	dhd_conf_set_intiovar(dhd, 0, WLC_SET_BAND, "WLC_SET_BAND", conf->band, 0, FALSE);
	dhd_conf_set_intiovar(dhd, 0, WLC_SET_VAR, "bcn_timeout", conf->bcn_timeout, 0, FALSE);
	dhd_conf_set_intiovar(dhd, 0, WLC_SET_PM, "WLC_SET_PM", conf->pm, 0, FALSE);
	dhd_conf_set_intiovar(dhd, 0, WLC_SET_SRL, "WLC_SET_SRL", conf->srl, 0, FALSE);
	dhd_conf_set_intiovar(dhd, 0, WLC_SET_LRL, "WLC_SET_LRL", conf->lrl, 0, FALSE);
	dhd_conf_set_bw_cap(dhd);
	dhd_conf_set_roam(dhd);

#if defined(BCMPCIE)
	dhd_conf_set_intiovar(dhd, 0, WLC_SET_VAR, "bus:deepsleep_disable",
		conf->bus_deepsleep_disable, 0, FALSE);
#endif /* defined(BCMPCIE) */

#ifdef IDHCP
	dhd_conf_set_intiovar(dhd, 0, WLC_SET_VAR, "dhcpc_enable", conf->dhcpc_enable,
		0, FALSE);
	if (conf->dhcpd_enable >= 0) {
		dhd_conf_set_bufiovar(dhd, 0, WLC_SET_VAR, "dhcpd_ip_addr",
			(char *)&conf->dhcpd_ip_addr, sizeof(conf->dhcpd_ip_addr), FALSE);
		dhd_conf_set_bufiovar(dhd, 0, WLC_SET_VAR, "dhcpd_ip_mask",
			(char *)&conf->dhcpd_ip_mask, sizeof(conf->dhcpd_ip_mask), FALSE);
		dhd_conf_set_bufiovar(dhd, 0, WLC_SET_VAR, "dhcpd_ip_start",
			(char *)&conf->dhcpd_ip_start, sizeof(conf->dhcpd_ip_start), FALSE);
		dhd_conf_set_bufiovar(dhd, 0, WLC_SET_VAR, "dhcpd_ip_end",
			(char *)&conf->dhcpd_ip_end, sizeof(conf->dhcpd_ip_end), FALSE);
		dhd_conf_set_intiovar(dhd, 0, WLC_SET_VAR, "dhcpd_enable",
			conf->dhcpd_enable, 0, FALSE);
	}
#endif
	dhd_conf_set_intiovar(dhd, 0, WLC_SET_FAKEFRAG, "WLC_SET_FAKEFRAG",
		conf->frameburst, 0, FALSE);

	dhd_conf_set_wl_cmd(dhd, wl_preinit, TRUE);
#if defined(BCMSDIO)
	if (conf->chip == BCM43751_CHIP_ID || conf->chip == BCM43752_CHIP_ID) {
		char ampdu_mpdu[] = "ampdu_mpdu=32";
		dhd_conf_set_wl_cmd(dhd, ampdu_mpdu, TRUE);
	} else {
		char ampdu_mpdu[] = "ampdu_mpdu=16";
		dhd_conf_set_wl_cmd(dhd, ampdu_mpdu, TRUE);
	}
#endif

#ifdef DHD_TPUT_PATCH
	if (dhd->conf->mtu)
		dhd_change_mtu(dhd, dhd->conf->mtu, 0);
#endif
	if (conf->chip == BCM4354_CHIP_ID || conf->chip == BCM4356_CHIP_ID ||
			conf->chip == BCM4371_CHIP_ID || conf->chip == BCM4359_CHIP_ID ||
			conf->chip == BCM43569_CHIP_ID ||
			conf->chip == BCM43751_CHIP_ID || conf->chip == BCM43752_CHIP_ID ||
			conf->chip == BCM4375_CHIP_ID) {
		dhd_conf_set_intiovar(dhd, 0, WLC_SET_VAR, "txbf", 1, 0, FALSE);
	}
	if (conf->chip == BCM4375_CHIP_ID) {
		char he_cmd[] = "110=1, nmode=1, vhtmode=1, he=enab 1";
		dhd_conf_set_wl_cmd(dhd, he_cmd, TRUE);
	}
	if (conf->chip == BCM43752_CHIP_ID || conf->chip == BCM4359_CHIP_ID) {
		char txack_alive[] = "txack_alive=0";
		dhd_conf_set_wl_cmd(dhd, txack_alive, TRUE);
	}
#ifdef WLDWDS
	{
		char dwds[] = "dwds=1";
		dhd_conf_set_wl_cmd(dhd, dwds, TRUE);
	}
#endif /* WLDWDS */
#if defined(WLEASYMESH)
	if (conf->fw_type == FW_TYPE_EZMESH) {
		if (conf->chip == BCM4359_CHIP_ID) {
			char ezmesh[] = "mbss=1, rsdb_mode=0";
			dhd_conf_set_wl_cmd(dhd, ezmesh, TRUE);
		} else {
			char ezmesh[] = "mbss=1";
			dhd_conf_set_wl_cmd(dhd, ezmesh, TRUE);
		}
	}
#endif /* WLEASYMESH */
#if defined(BCMSDIO)
	if (conf->devid == BCM43751_CHIP_ID)
#elif defined(BCMPCIE)
	if (conf->devid == BCM43751_D11AX_ID)
#endif
	{
		if (FW_SUPPORTED(dhd, mbo)) {
			char he_features[] = "he=enab 0,he=features 0";
			dhd_conf_set_wl_cmd(dhd, he_features, TRUE);
		}
	}
#ifdef UPDATE_MODULE_NAME
	dhd_conf_compat_func(dhd);
#endif
#ifndef SUPPORT_RANDOM_MAC_SCAN
	{
		char scanmac[] = "scanmac=enable 0";
		dhd_conf_set_wl_cmd(dhd, scanmac, TRUE);
	}
#endif
	dhd_conf_set_wl_cmd(dhd, conf->wl_preinit, TRUE);

#ifndef WL_CFG80211
	dhd_conf_set_intiovar(dhd, 0, WLC_UP, "WLC_UP", 0, 0, FALSE);
#endif

}

int
dhd_conf_preinit(dhd_pub_t *dhd)
{
	struct dhd_conf *conf = dhd->conf;

	CONFIG_TRACE("Enter\n");

#ifdef SET_FWNV_BY_MAC
	dhd_conf_free_mac_list(&conf->fw_by_mac);
	dhd_conf_free_mac_list(&conf->nv_by_mac);
#endif
	dhd_conf_free_chip_nv_path_list(&conf->nv_by_chip);
	dhd_conf_free_country_list(conf);
	dhd_conf_free_mchan_list(conf);
#ifdef PKT_FILTER_SUPPORT
	if (conf->magic_pkt_filter_add) {
		kfree(conf->magic_pkt_filter_add);
		conf->magic_pkt_filter_add = NULL;
	}
#endif
	if (conf->wl_preinit) {
		kfree(conf->wl_preinit);
		conf->wl_preinit = NULL;
	}
	if (conf->wl_suspend) {
		kfree(conf->wl_suspend);
		conf->wl_suspend = NULL;
	}
	if (conf->wl_resume) {
		kfree(conf->wl_resume);
		conf->wl_resume = NULL;
	}
	if (conf->vndr_ie_assocreq) {
		kfree(conf->vndr_ie_assocreq);
		conf->vndr_ie_assocreq = NULL;
	}
	conf->band = -1;
	memset(&conf->bw_cap, -1, sizeof(conf->bw_cap));
	if (conf->chip == BCM43362_CHIP_ID || conf->chip == BCM4330_CHIP_ID) {
		strcpy(conf->cspec.country_abbrev, "ALL");
		strcpy(conf->cspec.ccode, "ALL");
		conf->cspec.rev = 0;
	}
	else if (conf->chip == BCM4335_CHIP_ID || conf->chip == BCM4339_CHIP_ID ||
			conf->chip == BCM4354_CHIP_ID || conf->chip == BCM4356_CHIP_ID ||
			conf->chip == BCM4345_CHIP_ID || conf->chip == BCM4371_CHIP_ID ||
			conf->chip == BCM43569_CHIP_ID || conf->chip == BCM4359_CHIP_ID ||
			conf->chip == BCM4375_CHIP_ID) {
		strcpy(conf->cspec.country_abbrev, "CN");
		strcpy(conf->cspec.ccode, "CN");
		conf->cspec.rev = 38;
	}
	else {
		strcpy(conf->cspec.country_abbrev, "CN");
		strcpy(conf->cspec.ccode, "CN");
		conf->cspec.rev = 0;
	}
	memset(&conf->channels, 0, sizeof(wl_channel_list_t));
	conf->roam_off = 1;
	conf->roam_off_suspend = 1;
	conf->roam_trigger[0] = -65;
	conf->roam_trigger[1] = WLC_BAND_ALL;
	conf->roam_scan_period[0] = 10;
	conf->roam_scan_period[1] = WLC_BAND_ALL;
	conf->roam_delta[0] = 10;
	conf->roam_delta[1] = WLC_BAND_ALL;
	conf->fullroamperiod = 20;
	conf->keep_alive_period = 30000;
#ifdef ARP_OFFLOAD_SUPPORT
	conf->garp = FALSE;
#endif
	conf->force_wme_ac = 0;
	memset(&conf->wme_sta, 0, sizeof(wme_param_t));
	memset(&conf->wme_ap, 0, sizeof(wme_param_t));
#ifdef PKT_FILTER_SUPPORT
	memset(&conf->pkt_filter_add, 0, sizeof(conf_pkt_filter_add_t));
	memset(&conf->pkt_filter_del, 0, sizeof(conf_pkt_filter_del_t));
#endif
	conf->srl = -1;
	conf->lrl = -1;
	conf->bcn_timeout = 16;
	conf->disable_proptx = -1;
	conf->dhd_poll = -1;
#ifdef BCMSDIO
	conf->use_rxchain = 0;
	conf->bus_rxglom = TRUE;
	conf->txglom_ext = FALSE;
	conf->tx_max_offset = 0;
	conf->txglomsize = SDPCM_DEFGLOM_SIZE;
	conf->txctl_tmo_fix = 300;
	conf->txglom_mode = SDPCM_TXGLOM_CPY;
	conf->deferred_tx_len = 0;
	conf->dhd_txminmax = 1;
	conf->txinrx_thres = -1;
#ifdef MINIME
	conf->ramsize = 0x80000;
#endif
#if defined(SDIO_ISR_THREAD)
	conf->intr_extn = FALSE;
#endif
#ifdef BCMSDIO_RXLIM_POST
	conf->rxlim_en = FALSE;
#endif
#ifdef BCMSDIO_TXSEQ_SYNC
	conf->txseq_sync = FALSE;
#endif
#if defined(HW_OOB)
	conf->oob_enabled_later = FALSE;
#endif
#ifdef BCMSDIO_INTSTATUS_WAR
	conf->read_intr_mode = 0;
#endif
	conf->kso_try_max = 0;
#ifdef KSO_DEBUG
	memset(&conf->kso_try_array, 0, sizeof(conf->kso_try_array));
#endif
#endif
#ifdef BCMPCIE
	conf->bus_deepsleep_disable = 1;
	conf->flow_ring_queue_threshold = FLOW_RING_QUEUE_THRESHOLD;
	conf->d2h_intr_method = -1;
	conf->d2h_intr_control = -1;
	conf->enq_hdr_pkt = 0;
#endif
	conf->dpc_cpucore = -1;
	conf->rxf_cpucore = -1;
	conf->dhd_dpc_prio = -1;
	conf->frameburst = -1;
	conf->deepsleep = FALSE;
	conf->pm = -1;
	conf->pm_in_suspend = -1;
	conf->insuspend = 0;
	conf->suspend_mode = PM_NOTIFIER;
	conf->suspend_bcn_li_dtim = -1;
	conf->rekey_offload = FALSE;
#ifdef WL_EXT_WOWL
	dhd_master_mode = TRUE;
	conf->wowl = WL_WOWL_NET|WL_WOWL_DIS|WL_WOWL_BCN;
	conf->insuspend |= (WOWL_IN_SUSPEND | NO_TXDATA_IN_SUSPEND);
#endif
	if (conf->suspend_mode == PM_NOTIFIER || conf->suspend_mode == SUSPEND_MODE_2)
		conf->insuspend |= (NO_TXDATA_IN_SUSPEND | NO_TXCTL_IN_SUSPEND);
	conf->suspended = FALSE;
	memset(&conf->bssid_insuspend, 0, ETHER_ADDR_LEN);
#ifdef SUSPEND_EVENT
	memset(&conf->resume_eventmask, 0, sizeof(conf->resume_eventmask));
	conf->wlfc = FALSE;
#endif
#ifdef GET_CUSTOM_MAC_FROM_CONFIG
	memset(&conf->hw_ether, 0, sizeof(conf->hw_ether));
#endif
#ifdef IDHCP
	conf->dhcpc_enable = -1;
	conf->dhcpd_enable = -1;
#endif
	conf->orphan_move = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	conf->tsq = 10;
#else
	conf->tsq = 0;
#endif
#ifdef DHDTCPACK_SUPPRESS
#ifdef BCMPCIE
	conf->tcpack_sup_mode = TCPACK_SUP_HOLD;
#else
	conf->tcpack_sup_mode = TCPACK_SUP_OFF;
#endif
	conf->tcpack_sup_ratio = CUSTOM_TCPACK_SUPP_RATIO;
	conf->tcpack_sup_delay = CUSTOM_TCPACK_DELAY_TIME;
#endif
	conf->pktprio8021x = -1;
	conf->ctrl_resched = 2;
	conf->rxcnt_timeout = 3;
	conf->in4way = STA_NO_SCAN_IN4WAY | STA_WAIT_DISCONNECTED |
		AP_WAIT_STA_RECONNECT;
	if (conf->chip == BCM43752_CHIP_ID)
		conf->war = SET_CHAN_INCONN | FW_REINIT_INCSA | FW_REINIT_EMPTY_SCAN;
	else
		conf->war = 0;
#ifdef P2P_AP_CONCURRENT
	conf->war |= P2P_AP_MAC_CONFLICT;
#endif
#ifdef PROPTX_MAXCOUNT
	conf->proptx_maxcnt_2g = 46;
	conf->proptx_maxcnt_5g = WL_TXSTATUS_FREERUNCTR_MASK;
#endif /* DYNAMIC_PROPTX_MAXCOUNT */
#ifdef TPUT_MONITOR
	conf->data_drop_mode = NO_DATA_DROP;
	conf->tput_monitor_ms = 0;
#ifdef BCMSDIO
	if (conf->chip == BCM43752_CHIP_ID || conf->chip == BCM4375_CHIP_ID)
		conf->doflow_tput_thresh = 200;
	else
		conf->doflow_tput_thresh = 9999;
	if (conf->doflow_tput_thresh > 0 && conf->doflow_tput_thresh < 9999)
		conf->tput_monitor_ms = 1000;
#endif
#endif
#ifdef SCAN_SUPPRESS
	conf->scan_intput = SCAN_CURCHAN_INTPUT;
	conf->scan_busy_thresh = 10;
	conf->scan_busy_tmo = 120;
	if (conf->chip == BCM43752_CHIP_ID || conf->chip == BCM4375_CHIP_ID)
		conf->scan_tput_thresh = 100;
	else
		conf->scan_tput_thresh = 9999;
	if (conf->scan_tput_thresh > 0 && conf->scan_tput_thresh < 9999)
		conf->tput_monitor_ms = 1000;
#endif
#ifdef DHD_TPUT_PATCH
	conf->tput_patch = FALSE;
	conf->mtu = 0;
	conf->pktsetsum = FALSE;
#endif
#ifdef SET_XPS_CPUS
	conf->xps_cpus = FALSE;
#endif
#ifdef SET_RPS_CPUS
	conf->rps_cpus = FALSE;
#endif
#ifdef CHECK_DOWNLOAD_FW
	conf->fwchk = FALSE;
#endif
#ifdef ISAM_PREINIT
	memset(conf->isam_init, 0, sizeof(conf->isam_init));
	memset(conf->isam_config, 0, sizeof(conf->isam_config));
	memset(conf->isam_enable, 0, sizeof(conf->isam_enable));
#endif
#if defined(SDIO_ISR_THREAD)
	if (conf->chip == BCM43012_CHIP_ID ||
			conf->chip == BCM4335_CHIP_ID || conf->chip == BCM4339_CHIP_ID ||
			conf->chip == BCM43454_CHIP_ID || conf->chip == BCM4345_CHIP_ID ||
			conf->chip == BCM4354_CHIP_ID || conf->chip == BCM4356_CHIP_ID ||
			conf->chip == BCM4345_CHIP_ID || conf->chip == BCM4371_CHIP_ID ||
			conf->chip == BCM4359_CHIP_ID ||
			conf->chip == BCM43751_CHIP_ID || conf->chip == BCM43752_CHIP_ID ||
			conf->chip == BCM4375_CHIP_ID) {
		conf->intr_extn = TRUE;
	}
#endif
	if ((conf->chip == BCM43430_CHIP_ID && conf->chiprev == 2) ||
			conf->chip == BCM43012_CHIP_ID ||
			conf->chip == BCM4335_CHIP_ID || conf->chip == BCM4339_CHIP_ID ||
			conf->chip == BCM43454_CHIP_ID || conf->chip == BCM4345_CHIP_ID ||
			conf->chip == BCM4354_CHIP_ID || conf->chip == BCM4356_CHIP_ID ||
			conf->chip == BCM4345_CHIP_ID || conf->chip == BCM4371_CHIP_ID ||
			conf->chip == BCM43569_CHIP_ID || conf->chip == BCM4359_CHIP_ID ||
			conf->chip == BCM43751_CHIP_ID || conf->chip == BCM43752_CHIP_ID ||
			conf->chip == BCM4375_CHIP_ID) {
#ifdef DHDTCPACK_SUPPRESS
#ifdef BCMSDIO
		conf->tcpack_sup_mode = TCPACK_SUP_REPLACE;
#endif
#endif
#if defined(BCMSDIO) || defined(BCMPCIE)
		dhd_rxbound = 128;
		dhd_txbound = 64;
#endif
		conf->frameburst = 1;
#ifdef BCMSDIO
		conf->dhd_txminmax = -1;
		conf->txinrx_thres = 128;
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
		conf->orphan_move = 1;
#else
		conf->orphan_move = 0;
#endif
	}
#ifdef DHD_TPUT_PATCH
	if (conf->chip == BCM43751_CHIP_ID || conf->chip == BCM43752_CHIP_ID ||
			conf->chip == BCM4375_CHIP_ID) {
		conf->tput_patch = TRUE;
		dhd_conf_set_tput_patch(dhd);
	}
#endif

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
	init_waitqueue_head(&conf->event_complete);
#ifdef CUSTOMER_HW_ROCKCHIP
#ifdef BCMPCIE
	conf->d2h_intr_control = 0;
#endif
#endif

	return 0;
}

int
dhd_conf_reset(dhd_pub_t *dhd)
{
	struct dhd_conf *conf = dhd->conf;

#ifdef SET_FWNV_BY_MAC
	dhd_conf_free_mac_list(&conf->fw_by_mac);
	dhd_conf_free_mac_list(&conf->nv_by_mac);
#endif
	dhd_conf_free_chip_nv_path_list(&conf->nv_by_chip);
	dhd_conf_free_country_list(conf);
	dhd_conf_free_mchan_list(conf);
#ifdef PKT_FILTER_SUPPORT
	if (conf->magic_pkt_filter_add) {
		kfree(conf->magic_pkt_filter_add);
		conf->magic_pkt_filter_add = NULL;
	}
#endif
	if (conf->wl_preinit) {
		kfree(conf->wl_preinit);
		conf->wl_preinit = NULL;
	}
	if (conf->wl_suspend) {
		kfree(conf->wl_suspend);
		conf->wl_suspend = NULL;
	}
	if (conf->wl_resume) {
		kfree(conf->wl_resume);
		conf->wl_resume = NULL;
	}
	if (conf->vndr_ie_assocreq) {
		kfree(conf->vndr_ie_assocreq);
		conf->vndr_ie_assocreq = NULL;
	}
	memset(conf, 0, sizeof(dhd_conf_t));
	return 0;
}

int
dhd_conf_attach(dhd_pub_t *dhd)
{
	dhd_conf_t *conf;

	CONFIG_TRACE("Enter\n");

	if (dhd->conf != NULL) {
		CONFIG_MSG("config is attached before!\n");
		return 0;
	}
	/* Allocate private bus interface state */
	if (!(conf = MALLOC(dhd->osh, sizeof(dhd_conf_t)))) {
		CONFIG_ERROR("MALLOC failed\n");
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
	struct dhd_conf *conf = dhd->conf;

	CONFIG_TRACE("Enter\n");
	if (dhd->conf) {
#ifdef SET_FWNV_BY_MAC
		dhd_conf_free_mac_list(&conf->fw_by_mac);
		dhd_conf_free_mac_list(&conf->nv_by_mac);
#endif
		dhd_conf_free_chip_nv_path_list(&conf->nv_by_chip);
		dhd_conf_free_country_list(conf);
		dhd_conf_free_mchan_list(conf);
#ifdef PKT_FILTER_SUPPORT
		if (conf->magic_pkt_filter_add) {
			kfree(conf->magic_pkt_filter_add);
			conf->magic_pkt_filter_add = NULL;
		}
#endif
		if (conf->wl_preinit) {
			kfree(conf->wl_preinit);
			conf->wl_preinit = NULL;
		}
		if (conf->wl_suspend) {
			kfree(conf->wl_suspend);
			conf->wl_suspend = NULL;
		}
		if (conf->wl_resume) {
			kfree(conf->wl_resume);
			conf->wl_resume = NULL;
		}
		if (conf->vndr_ie_assocreq) {
			kfree(conf->vndr_ie_assocreq);
			conf->vndr_ie_assocreq = NULL;
		}
		MFREE(dhd->osh, conf, sizeof(dhd_conf_t));
	}
	dhd->conf = NULL;
}
