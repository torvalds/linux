// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/string.h>
#include <bcm47xx.h>
#include <bcm47xx_board.h>

struct bcm47xx_board_type {
	const enum bcm47xx_board board;
	const char *name;
};

struct bcm47xx_board_type_list1 {
	struct bcm47xx_board_type board;
	const char *value1;
};

struct bcm47xx_board_type_list2 {
	struct bcm47xx_board_type board;
	const char *value1;
	const char *value2;
};

struct bcm47xx_board_type_list3 {
	struct bcm47xx_board_type board;
	const char *value1;
	const char *value2;
	const char *value3;
};

struct bcm47xx_board_store {
	enum bcm47xx_board board;
	char name[BCM47XX_BOARD_MAX_NAME];
};

/* model_name */
static const
struct bcm47xx_board_type_list1 bcm47xx_board_list_model_name[] __initconst = {
	{{BCM47XX_BOARD_DLINK_DIR130, "D-Link DIR-130"}, "DIR-130"},
	{{BCM47XX_BOARD_DLINK_DIR330, "D-Link DIR-330"}, "DIR-330"},
	{ {0}, NULL},
};

/* hardware_version */
static const
struct bcm47xx_board_type_list1 bcm47xx_board_list_hardware_version[] __initconst = {
	{{BCM47XX_BOARD_ASUS_RTN10U, "Asus RT-N10U"}, "RTN10U"},
	{{BCM47XX_BOARD_ASUS_RTN10D, "Asus RT-N10D"}, "RTN10D"},
	{{BCM47XX_BOARD_ASUS_RTN12, "Asus RT-N12"}, "RT-N12"},
	{{BCM47XX_BOARD_ASUS_RTN12B1, "Asus RT-N12B1"}, "RTN12B1"},
	{{BCM47XX_BOARD_ASUS_RTN12C1, "Asus RT-N12C1"}, "RTN12C1"},
	{{BCM47XX_BOARD_ASUS_RTN12D1, "Asus RT-N12D1"}, "RTN12D1"},
	{{BCM47XX_BOARD_ASUS_RTN12HP, "Asus RT-N12HP"}, "RTN12HP"},
	{{BCM47XX_BOARD_ASUS_RTN16, "Asus RT-N16"}, "RT-N16-"},
	{{BCM47XX_BOARD_ASUS_WL320GE, "Asus WL320GE"}, "WL320G-"},
	{{BCM47XX_BOARD_ASUS_WL330GE, "Asus WL330GE"}, "WL330GE-"},
	{{BCM47XX_BOARD_ASUS_WL500GD, "Asus WL500GD"}, "WL500gd-"},
	{{BCM47XX_BOARD_ASUS_WL500GPV1, "Asus WL500GP V1"}, "WL500gp-"},
	{{BCM47XX_BOARD_ASUS_WL500GPV2, "Asus WL500GP V2"}, "WL500GPV2-"},
	{{BCM47XX_BOARD_ASUS_WL500W, "Asus WL500W"}, "WL500gW-"},
	{{BCM47XX_BOARD_ASUS_WL520GC, "Asus WL520GC"}, "WL520GC-"},
	{{BCM47XX_BOARD_ASUS_WL520GU, "Asus WL520GU"}, "WL520GU-"},
	{{BCM47XX_BOARD_BELKIN_F7D3301, "Belkin F7D3301"}, "F7D3301"},
	{{BCM47XX_BOARD_BELKIN_F7D3302, "Belkin F7D3302"}, "F7D3302"},
	{{BCM47XX_BOARD_BELKIN_F7D4301, "Belkin F7D4301"}, "F7D4301"},
	{{BCM47XX_BOARD_BELKIN_F7D4302, "Belkin F7D4302"}, "F7D4302"},
	{{BCM47XX_BOARD_BELKIN_F7D4401, "Belkin F7D4401"}, "F7D4401"},
	{ {0}, NULL},
};

/* hardware_version, boardnum */
static const
struct bcm47xx_board_type_list2 bcm47xx_board_list_hw_version_num[] __initconst = {
	{{BCM47XX_BOARD_MICROSOFT_MN700, "Microsoft MN-700"}, "WL500-", "mn700"},
	{{BCM47XX_BOARD_ASUS_WL500G, "Asus WL500G"}, "WL500-", "asusX"},
	{ {0}, NULL},
};

/* productid */
static const
struct bcm47xx_board_type_list1 bcm47xx_board_list_productid[] __initconst = {
	{{BCM47XX_BOARD_ASUS_RTAC66U, "Asus RT-AC66U"}, "RT-AC66U"},
	{{BCM47XX_BOARD_ASUS_RTN10, "Asus RT-N10"}, "RT-N10"},
	{{BCM47XX_BOARD_ASUS_RTN10D, "Asus RT-N10D"}, "RT-N10D"},
	{{BCM47XX_BOARD_ASUS_RTN15U, "Asus RT-N15U"}, "RT-N15U"},
	{{BCM47XX_BOARD_ASUS_RTN16, "Asus RT-N16"}, "RT-N16"},
	{{BCM47XX_BOARD_ASUS_RTN53, "Asus RT-N53"}, "RT-N53"},
	{{BCM47XX_BOARD_ASUS_RTN66U, "Asus RT-N66U"}, "RT-N66U"},
	{{BCM47XX_BOARD_ASUS_WL300G, "Asus WL300G"}, "WL300g"},
	{{BCM47XX_BOARD_ASUS_WLHDD, "Asus WLHDD"}, "WLHDD"},
	{ {0}, NULL},
};

/* ModelId */
static const
struct bcm47xx_board_type_list1 bcm47xx_board_list_ModelId[] __initconst = {
	{{BCM47XX_BOARD_DELL_TM2300, "Dell TrueMobile 2300"}, "WX-5565"},
	{{BCM47XX_BOARD_MOTOROLA_WE800G, "Motorola WE800G"}, "WE800G"},
	{{BCM47XX_BOARD_MOTOROLA_WR850GP, "Motorola WR850GP"}, "WR850GP"},
	{{BCM47XX_BOARD_MOTOROLA_WR850GV2V3, "Motorola WR850G"}, "WR850G"},
	{ {0}, NULL},
};

/* melco_id or buf1falo_id */
static const
struct bcm47xx_board_type_list1 bcm47xx_board_list_melco_id[] __initconst = {
	{{BCM47XX_BOARD_BUFFALO_WBR2_G54, "Buffalo WBR2-G54"}, "29bb0332"},
	{{BCM47XX_BOARD_BUFFALO_WHR2_A54G54, "Buffalo WHR2-A54G54"}, "290441dd"},
	{{BCM47XX_BOARD_BUFFALO_WHR_G125, "Buffalo WHR-G125"}, "32093"},
	{{BCM47XX_BOARD_BUFFALO_WHR_G54S, "Buffalo WHR-G54S"}, "30182"},
	{{BCM47XX_BOARD_BUFFALO_WHR_HP_G54, "Buffalo WHR-HP-G54"}, "30189"},
	{{BCM47XX_BOARD_BUFFALO_WLA2_G54L, "Buffalo WLA2-G54L"}, "29129"},
	{{BCM47XX_BOARD_BUFFALO_WZR_G300N, "Buffalo WZR-G300N"}, "31120"},
	{{BCM47XX_BOARD_BUFFALO_WZR_RS_G54, "Buffalo WZR-RS-G54"}, "30083"},
	{{BCM47XX_BOARD_BUFFALO_WZR_RS_G54HP, "Buffalo WZR-RS-G54HP"}, "30103"},
	{ {0}, NULL},
};

/* boot_hw_model, boot_hw_ver */
static const
struct bcm47xx_board_type_list2 bcm47xx_board_list_boot_hw[] __initconst = {
	/* like WRT160N v3.0 */
	{{BCM47XX_BOARD_CISCO_M10V1, "Cisco M10"}, "M10", "1.0"},
	/* like WRT310N v2.0 */
	{{BCM47XX_BOARD_CISCO_M20V1, "Cisco M20"}, "M20", "1.0"},
	{{BCM47XX_BOARD_LINKSYS_E900V1, "Linksys E900 V1"}, "E900", "1.0"},
	/* like WRT160N v3.0 */
	{{BCM47XX_BOARD_LINKSYS_E1000V1, "Linksys E1000 V1"}, "E100", "1.0"},
	{{BCM47XX_BOARD_LINKSYS_E1000V2, "Linksys E1000 V2"}, "E1000", "2.0"},
	{{BCM47XX_BOARD_LINKSYS_E1000V21, "Linksys E1000 V2.1"}, "E1000", "2.1"},
	{{BCM47XX_BOARD_LINKSYS_E1200V2, "Linksys E1200 V2"}, "E1200", "2.0"},
	{{BCM47XX_BOARD_LINKSYS_E2000V1, "Linksys E2000 V1"}, "Linksys E2000", "1.0"},
	/* like WRT610N v2.0 */
	{{BCM47XX_BOARD_LINKSYS_E3000V1, "Linksys E3000 V1"}, "E300", "1.0"},
	{{BCM47XX_BOARD_LINKSYS_E3200V1, "Linksys E3200 V1"}, "E3200", "1.0"},
	{{BCM47XX_BOARD_LINKSYS_E4200V1, "Linksys E4200 V1"}, "E4200", "1.0"},
	{{BCM47XX_BOARD_LINKSYS_WRT150NV11, "Linksys WRT150N V1.1"}, "WRT150N", "1.1"},
	{{BCM47XX_BOARD_LINKSYS_WRT150NV1, "Linksys WRT150N V1"}, "WRT150N", "1"},
	{{BCM47XX_BOARD_LINKSYS_WRT160NV1, "Linksys WRT160N V1"}, "WRT160N", "1.0"},
	{{BCM47XX_BOARD_LINKSYS_WRT160NV3, "Linksys WRT160N V3"}, "WRT160N", "3.0"},
	{{BCM47XX_BOARD_LINKSYS_WRT300NV11, "Linksys WRT300N V1.1"}, "WRT300N", "1.1"},
	{{BCM47XX_BOARD_LINKSYS_WRT310NV1, "Linksys WRT310N V1"}, "WRT310N", "1.0"},
	{{BCM47XX_BOARD_LINKSYS_WRT310NV2, "Linksys WRT310N V2"}, "WRT310N", "2.0"},
	{{BCM47XX_BOARD_LINKSYS_WRT54G3GV2, "Linksys WRT54G3GV2-VF"}, "WRT54G3GV2-VF", "1.0"},
	{{BCM47XX_BOARD_LINKSYS_WRT610NV1, "Linksys WRT610N V1"}, "WRT610N", "1.0"},
	{{BCM47XX_BOARD_LINKSYS_WRT610NV2, "Linksys WRT610N V2"}, "WRT610N", "2.0"},
	{ {0}, NULL},
};

/* board_id */
static const
struct bcm47xx_board_type_list1 bcm47xx_board_list_board_id[] __initconst = {
	{{BCM47XX_BOARD_LUXUL_ABR_4400_V1, "Luxul ABR-4400 V1"}, "luxul_abr4400_v1"},
	{{BCM47XX_BOARD_LUXUL_XAP_310_V1, "Luxul XAP-310 V1"}, "luxul_xap310_v1"},
	{{BCM47XX_BOARD_LUXUL_XAP_1210_V1, "Luxul XAP-1210 V1"}, "luxul_xap1210_v1"},
	{{BCM47XX_BOARD_LUXUL_XAP_1230_V1, "Luxul XAP-1230 V1"}, "luxul_xap1230_v1"},
	{{BCM47XX_BOARD_LUXUL_XAP_1240_V1, "Luxul XAP-1240 V1"}, "luxul_xap1240_v1"},
	{{BCM47XX_BOARD_LUXUL_XAP_1500_V1, "Luxul XAP-1500 V1"}, "luxul_xap1500_v1"},
	{{BCM47XX_BOARD_LUXUL_XBR_4400_V1, "Luxul XBR-4400 V1"}, "luxul_xbr4400_v1"},
	{{BCM47XX_BOARD_LUXUL_XVW_P30_V1, "Luxul XVW-P30 V1"}, "luxul_xvwp30_v1"},
	{{BCM47XX_BOARD_LUXUL_XWR_600_V1, "Luxul XWR-600 V1"}, "luxul_xwr600_v1"},
	{{BCM47XX_BOARD_LUXUL_XWR_1750_V1, "Luxul XWR-1750 V1"}, "luxul_xwr1750_v1"},
	{{BCM47XX_BOARD_NETGEAR_R6200_V1, "Netgear R6200 V1"}, "U12H192T00_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WGR614V8, "Netgear WGR614 V8"}, "U12H072T00_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WGR614V9, "Netgear WGR614 V9"}, "U12H094T00_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WGR614_V10, "Netgear WGR614 V10"}, "U12H139T01_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WNDR3300, "Netgear WNDR3300"}, "U12H093T00_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WNDR3400V1, "Netgear WNDR3400 V1"}, "U12H155T00_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WNDR3400V2, "Netgear WNDR3400 V2"}, "U12H187T00_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WNDR3400_V3, "Netgear WNDR3400 V3"}, "U12H208T00_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WNDR3400VCNA, "Netgear WNDR3400 Vcna"}, "U12H155T01_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WNDR3700V3, "Netgear WNDR3700 V3"}, "U12H194T00_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WNDR4000, "Netgear WNDR4000"}, "U12H181T00_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WNDR4500V1, "Netgear WNDR4500 V1"}, "U12H189T00_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WNDR4500V2, "Netgear WNDR4500 V2"}, "U12H224T00_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WNR1000_V3, "Netgear WNR1000 V3"}, "U12H139T00_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WNR1000_V3, "Netgear WNR1000 V3"}, "U12H139T50_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WNR2000, "Netgear WNR2000"}, "U12H114T00_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WNR3500L, "Netgear WNR3500L"}, "U12H136T99_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WNR3500U, "Netgear WNR3500U"}, "U12H136T00_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WNR3500V2, "Netgear WNR3500 V2"}, "U12H127T00_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WNR3500V2VC, "Netgear WNR3500 V2vc"}, "U12H127T70_NETGEAR"},
	{{BCM47XX_BOARD_NETGEAR_WNR834BV2, "Netgear WNR834B V2"}, "U12H081T00_NETGEAR"},
	{ {0}, NULL},
};

/* boardtype, boardnum, boardrev */
static const
struct bcm47xx_board_type_list3 bcm47xx_board_list_board[] __initconst = {
	{{BCM47XX_BOARD_HUAWEI_E970, "Huawei E970"}, "0x048e", "0x5347", "0x11"},
	{{BCM47XX_BOARD_PHICOMM_M1, "Phicomm M1"}, "0x0590", "80", "0x1104"},
	{{BCM47XX_BOARD_ZTE_H218N, "ZTE H218N"}, "0x053d", "1234", "0x1305"},
	{{BCM47XX_BOARD_NETGEAR_WNR3500L, "Netgear WNR3500L"}, "0x04CF", "3500", "02"},
	{{BCM47XX_BOARD_LINKSYS_WRT54G_TYPE_0101, "Linksys WRT54G/GS/GL"}, "0x0101", "42", "0x10"},
	{{BCM47XX_BOARD_LINKSYS_WRT54G_TYPE_0467, "Linksys WRT54G/GS/GL"}, "0x0467", "42", "0x10"},
	{{BCM47XX_BOARD_LINKSYS_WRT54G_TYPE_0708, "Linksys WRT54G/GS/GL"}, "0x0708", "42", "0x10"},
	{ {0}, NULL},
};

/* boardtype, boardrev */
static const
struct bcm47xx_board_type_list2 bcm47xx_board_list_board_type_rev[] __initconst = {
	{{BCM47XX_BOARD_SIEMENS_SE505V2, "Siemens SE505 V2"}, "0x0101", "0x10"},
	{ {0}, NULL},
};

/*
 * Some devices don't use any common NVRAM entry for identification and they
 * have only one model specific variable.
 * They don't deserve own arrays, let's group them there using key-value array.
 */
static const
struct bcm47xx_board_type_list2 bcm47xx_board_list_key_value[] __initconst = {
	{{BCM47XX_BOARD_ASUS_WL700GE, "Asus WL700"}, "model_no", "WL700"},
	{{BCM47XX_BOARD_LINKSYS_WRT300N_V1, "Linksys WRT300N V1"}, "router_name", "WRT300N"},
	{{BCM47XX_BOARD_LINKSYS_WRT600N_V11, "Linksys WRT600N V1.1"}, "Model_Name", "WRT600N"},
	{{BCM47XX_BOARD_LINKSYS_WRTSL54GS, "Linksys WRTSL54GS"}, "machine_name", "WRTSL54GS"},
	{ {0}, NULL},
};

static const
struct bcm47xx_board_type bcm47xx_board_unknown[] __initconst = {
	{BCM47XX_BOARD_UNKNOWN, "Unknown Board"},
};

static struct bcm47xx_board_store bcm47xx_board = {BCM47XX_BOARD_NO, "Unknown Board"};

static __init const struct bcm47xx_board_type *bcm47xx_board_get_nvram(void)
{
	char buf1[30];
	char buf2[30];
	char buf3[30];
	const struct bcm47xx_board_type_list1 *e1;
	const struct bcm47xx_board_type_list2 *e2;
	const struct bcm47xx_board_type_list3 *e3;

	if (bcm47xx_nvram_getenv("model_name", buf1, sizeof(buf1)) >= 0) {
		for (e1 = bcm47xx_board_list_model_name; e1->value1; e1++) {
			if (!strcmp(buf1, e1->value1))
				return &e1->board;
		}
	}

	if (bcm47xx_nvram_getenv("hardware_version", buf1, sizeof(buf1)) >= 0) {
		for (e1 = bcm47xx_board_list_hardware_version; e1->value1; e1++) {
			if (strstarts(buf1, e1->value1))
				return &e1->board;
		}
	}

	if (bcm47xx_nvram_getenv("hardware_version", buf1, sizeof(buf1)) >= 0 &&
	    bcm47xx_nvram_getenv("boardnum", buf2, sizeof(buf2)) >= 0) {
		for (e2 = bcm47xx_board_list_hw_version_num; e2->value1; e2++) {
			if (!strstarts(buf1, e2->value1) &&
			    !strcmp(buf2, e2->value2))
				return &e2->board;
		}
	}

	if (bcm47xx_nvram_getenv("productid", buf1, sizeof(buf1)) >= 0) {
		for (e1 = bcm47xx_board_list_productid; e1->value1; e1++) {
			if (!strcmp(buf1, e1->value1))
				return &e1->board;
		}
	}

	if (bcm47xx_nvram_getenv("ModelId", buf1, sizeof(buf1)) >= 0) {
		for (e1 = bcm47xx_board_list_ModelId; e1->value1; e1++) {
			if (!strcmp(buf1, e1->value1))
				return &e1->board;
		}
	}

	if (bcm47xx_nvram_getenv("melco_id", buf1, sizeof(buf1)) >= 0 ||
	    bcm47xx_nvram_getenv("buf1falo_id", buf1, sizeof(buf1)) >= 0) {
		/* buffalo hardware, check id for specific hardware matches */
		for (e1 = bcm47xx_board_list_melco_id; e1->value1; e1++) {
			if (!strcmp(buf1, e1->value1))
				return &e1->board;
		}
	}

	if (bcm47xx_nvram_getenv("boot_hw_model", buf1, sizeof(buf1)) >= 0 &&
	    bcm47xx_nvram_getenv("boot_hw_ver", buf2, sizeof(buf2)) >= 0) {
		for (e2 = bcm47xx_board_list_boot_hw; e2->value1; e2++) {
			if (!strcmp(buf1, e2->value1) &&
			    !strcmp(buf2, e2->value2))
				return &e2->board;
		}
	}

	if (bcm47xx_nvram_getenv("board_id", buf1, sizeof(buf1)) >= 0) {
		for (e1 = bcm47xx_board_list_board_id; e1->value1; e1++) {
			if (!strcmp(buf1, e1->value1))
				return &e1->board;
		}
	}

	if (bcm47xx_nvram_getenv("boardtype", buf1, sizeof(buf1)) >= 0 &&
	    bcm47xx_nvram_getenv("boardnum", buf2, sizeof(buf2)) >= 0 &&
	    bcm47xx_nvram_getenv("boardrev", buf3, sizeof(buf3)) >= 0) {
		for (e3 = bcm47xx_board_list_board; e3->value1; e3++) {
			if (!strcmp(buf1, e3->value1) &&
			    !strcmp(buf2, e3->value2) &&
			    !strcmp(buf3, e3->value3))
				return &e3->board;
		}
	}

	if (bcm47xx_nvram_getenv("boardtype", buf1, sizeof(buf1)) >= 0 &&
	    bcm47xx_nvram_getenv("boardrev", buf2, sizeof(buf2)) >= 0 &&
	    bcm47xx_nvram_getenv("boardnum", buf3, sizeof(buf3)) ==  -ENOENT) {
		for (e2 = bcm47xx_board_list_board_type_rev; e2->value1; e2++) {
			if (!strcmp(buf1, e2->value1) &&
			    !strcmp(buf2, e2->value2))
				return &e2->board;
		}
	}

	for (e2 = bcm47xx_board_list_key_value; e2->value1; e2++) {
		if (bcm47xx_nvram_getenv(e2->value1, buf1, sizeof(buf1)) >= 0) {
			if (!strcmp(buf1, e2->value2))
				return &e2->board;
		}
	}

	return bcm47xx_board_unknown;
}

void __init bcm47xx_board_detect(void)
{
	int err;
	char buf[10];
	const struct bcm47xx_board_type *board_detected;

	if (bcm47xx_board.board != BCM47XX_BOARD_NO)
		return;

	/* check if the nvram is available */
	err = bcm47xx_nvram_getenv("boardtype", buf, sizeof(buf));

	/* init of nvram failed, probably too early now */
	if (err == -ENXIO)
		return;

	board_detected = bcm47xx_board_get_nvram();
	bcm47xx_board.board = board_detected->board;
	strlcpy(bcm47xx_board.name, board_detected->name,
		BCM47XX_BOARD_MAX_NAME);
}

enum bcm47xx_board bcm47xx_board_get(void)
{
	return bcm47xx_board.board;
}
EXPORT_SYMBOL(bcm47xx_board_get);

const char *bcm47xx_board_get_name(void)
{
	return bcm47xx_board.name;
}
EXPORT_SYMBOL(bcm47xx_board_get_name);
