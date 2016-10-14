/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2016 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef __BNXT_FW_HDR_H__
#define __BNXT_FW_HDR_H__

#define BNXT_FIRMWARE_BIN_SIGNATURE     0x1a4d4342	/* "BCM"+0x1a */

enum SUPPORTED_FAMILY {
	DEVICE_5702_3_4_FAMILY,		/* 0  - Denali, Vinson, K2 */
	DEVICE_5705_FAMILY,		/* 1  - Bachelor */
	DEVICE_SHASTA_FAMILY,		/* 2  - 5751 */
	DEVICE_5706_FAMILY,		/* 3  - Teton */
	DEVICE_5714_FAMILY,		/* 4  - Hamilton */
	DEVICE_STANFORD_FAMILY,		/* 5  - 5755 */
	DEVICE_STANFORD_ME_FAMILY,	/* 6  - 5756 */
	DEVICE_SOLEDAD_FAMILY,		/* 7  - 5761[E] */
	DEVICE_CILAI_FAMILY,		/* 8  - 57780/60/90/91 */
	DEVICE_ASPEN_FAMILY,		/* 9  - 57781/85/61/65/91/95 */
	DEVICE_ASPEN_PLUS_FAMILY,	/* 10 - 57786 */
	DEVICE_LOGAN_FAMILY,		/* 11 - Any device in the Logan family
					 */
	DEVICE_LOGAN_5762,		/* 12 - Logan Enterprise (aka Columbia)
					 */
	DEVICE_LOGAN_57767,		/* 13 - Logan Client */
	DEVICE_LOGAN_57787,		/* 14 - Logan Consumer */
	DEVICE_LOGAN_5725,		/* 15 - Logan Server (TruManage-enabled)
					 */
	DEVICE_SAWTOOTH_FAMILY,		/* 16 - 5717/18 */
	DEVICE_COTOPAXI_FAMILY,		/* 17 - 5719 */
	DEVICE_SNAGGLETOOTH_FAMILY,	/* 18 - 5720 */
	DEVICE_CUMULUS_FAMILY,		/* 19 - Cumulus/Whitney */
	MAX_DEVICE_FAMILY
};

enum SUPPORTED_CODE {
	CODE_ASF1,		/* 0  - ASF VERSION 1.03 <deprecated> */
	CODE_ASF2,		/* 1  - ASF VERSION 2.00 <deprecated> */
	CODE_PASSTHRU,		/* 2  - PassThru         <deprecated> */
	CODE_PT_SEC,		/* 3  - PassThru with security <deprecated> */
	CODE_UMP,		/* 4  - UMP                     <deprecated> */
	CODE_BOOT,		/* 5  - Bootcode */
	CODE_DASH,		/* 6  - TruManage (DASH + ASF + PMCI)
				 *	Management firmwares
				 */
	CODE_MCTP_PASSTHRU,	/* 7  - NCSI / MCTP Passt-hrough firmware */
	CODE_PM_OFFLOAD,	/* 8  - Power-Management Proxy Offload firmwares
				 */
	CODE_MDNS_SD_OFFLOAD,	/* 9  - Multicast DNS Service Discovery Proxys
				 *	Offload firmware
				 */
	CODE_DISC_OFFLOAD,	/* 10 - Discovery Offload firmware */
	CODE_MUSTANG,		/* 11 - I2C Error reporting APE firmwares
				 *	<deprecated>
				 */
	CODE_ARP_BATCH,		/* 12 - ARP Batch firmware */
	CODE_SMASH,		/* 13 - TruManage (SMASH + DCMI/IPMI + PMCI)
				 *	Management firmware
				 */
	CODE_APE_DIAG,		/* 14 - APE Test Diag firmware */
	CODE_APE_PATCH,		/* 15 - APE Patch firmware */
	CODE_TANG_PATCH,	/* 16 - TANG Patch firmware */
	CODE_KONG_FW,		/* 17 - KONG firmware */
	CODE_KONG_PATCH,	/* 18 - KONG Patch firmware */
	CODE_BONO_FW,		/* 19 - BONO firmware */
	CODE_BONO_PATCH,	/* 20 - BONO Patch firmware */
	CODE_CHIMP_PATCH,	/* 21 - ChiMP Patch firmware */

	MAX_CODE_TYPE,
};

enum SUPPORTED_MEDIA {
	MEDIA_COPPER,		/* 0 */
	MEDIA_FIBER,		/* 1 */
	MEDIA_NONE,		/* 2 */
	MEDIA_COPPER_FIBER,	/* 3 */
	MAX_MEDIA_TYPE,
};

struct bnxt_fw_header {
	__le32 signature;	/* constains the constant value of
				 * BNXT_Firmware_Bin_Signatures
				 */
	u8 flags;		/* reserved for ChiMP use */
	u8 code_type;		/* enum SUPPORTED_CODE */
	u8 device;		/* enum SUPPORTED_FAMILY */
	u8 media;		/* enum SUPPORTED_MEDIA */
	u8 version[16];		/* the null terminated version string to
				 * indicate the version of the
				 * file, this will be copied from the binary
				 * file version string
				 */
	u8 build;
	u8 revision;
	u8 minor_ver;
	u8 major_ver;
};

#endif
