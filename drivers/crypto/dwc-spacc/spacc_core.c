// SPDX-License-Identifier: GPL-2.0

#include <linux/of_device.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include "spacc_hal.h"
#include "spacc_core.h"

static const u8 spacc_ctrl_map[SPACC_CTRL_VER_SIZE][SPACC_CTRL_MAPSIZE] = {
	{ 0, 8, 4, 12, 24, 16, 31, 25, 26, 27, 28, 29, 14, 15 },
	{ 0, 8, 3, 12, 24, 16, 31, 25, 26, 27, 28, 29, 14, 15 },
	{ 0, 4, 8, 13, 15, 16, 24, 25, 26, 27, 28, 29, 30, 31 }
};

static const int keysizes[2][7] = {
	/*   1    2   4   8  16  32   64 */
	{ 5,   8, 16, 24, 32,  0,   0 },  /* cipher key sizes*/
	{ 8,  16, 20, 24, 32, 64, 128 },  /* hash key sizes*/
};


/* bits are 40, 64, 128, 192, 256, and top bit for hash */
static const unsigned char template[] = {
	[CRYPTO_MODE_NULL]              = 0,
	[CRYPTO_MODE_AES_ECB]           = 28,	/* AESECB 128/224/256 */
	[CRYPTO_MODE_AES_CBC]           = 28,	/* AESCBC 128/224/256 */
	[CRYPTO_MODE_AES_CTR]           = 28,	/* AESCTR 128/224/256 */
	[CRYPTO_MODE_AES_CCM]           = 28,	/* AESCCM 128/224/256 */
	[CRYPTO_MODE_AES_GCM]           = 28,	/* AESGCM 128/224/256 */
	[CRYPTO_MODE_AES_F8]            = 28,	/* AESF8  128/224/256 */
	[CRYPTO_MODE_AES_XTS]           = 20,	/* AESXTS 128/256 */
	[CRYPTO_MODE_AES_CFB]           = 28,	/* AESCFB 128/224/256 */
	[CRYPTO_MODE_AES_OFB]           = 28,	/* AESOFB 128/224/256 */
	[CRYPTO_MODE_AES_CS1]           = 28,	/* AESCS1 128/224/256 */
	[CRYPTO_MODE_AES_CS2]           = 28,	/* AESCS2 128/224/256 */
	[CRYPTO_MODE_AES_CS3]           = 28,	/* AESCS3 128/224/256 */
	[CRYPTO_MODE_MULTI2_ECB]        = 0,	/* MULTI2 */
	[CRYPTO_MODE_MULTI2_CBC]        = 0,	/* MULTI2 */
	[CRYPTO_MODE_MULTI2_OFB]        = 0,	/* MULTI2 */
	[CRYPTO_MODE_MULTI2_CFB]        = 0,	/* MULTI2 */
	[CRYPTO_MODE_3DES_CBC]          = 8,	/* 3DES CBC */
	[CRYPTO_MODE_3DES_ECB]          = 8,	/* 3DES ECB */
	[CRYPTO_MODE_DES_CBC]           = 2,	/* DES CBC */
	[CRYPTO_MODE_DES_ECB]           = 2,	/* DES ECB */
	[CRYPTO_MODE_KASUMI_ECB]        = 4,	/* KASUMI ECB */
	[CRYPTO_MODE_KASUMI_F8]         = 4,	/* KASUMI F8 */
	[CRYPTO_MODE_SNOW3G_UEA2]       = 4,	/* SNOW3G */
	[CRYPTO_MODE_ZUC_UEA3]          = 4,	/* ZUC */
	[CRYPTO_MODE_CHACHA20_STREAM]   = 16,	/* CHACHA20 */
	[CRYPTO_MODE_CHACHA20_POLY1305] = 16,	/* CHACHA20 */
	[CRYPTO_MODE_SM4_ECB]           = 4,	/* SM4ECB 128 */
	[CRYPTO_MODE_SM4_CBC]           = 4,	/* SM4CBC 128 */
	[CRYPTO_MODE_SM4_CFB]           = 4,	/* SM4CFB 128 */
	[CRYPTO_MODE_SM4_OFB]           = 4,	/* SM4OFB 128 */
	[CRYPTO_MODE_SM4_CTR]           = 4,	/* SM4CTR 128 */
	[CRYPTO_MODE_SM4_CCM]           = 4,	/* SM4CCM 128 */
	[CRYPTO_MODE_SM4_GCM]           = 4,	/* SM4GCM 128 */
	[CRYPTO_MODE_SM4_F8]            = 4,	/* SM4F8  128 */
	[CRYPTO_MODE_SM4_XTS]           = 4,	/* SM4XTS 128 */
	[CRYPTO_MODE_SM4_CS1]           = 4,	/* SM4CS1 128 */
	[CRYPTO_MODE_SM4_CS2]           = 4,	/* SM4CS2 128 */
	[CRYPTO_MODE_SM4_CS3]           = 4,	/* SM4CS3 128 */

	[CRYPTO_MODE_HASH_MD5]          = 242,
	[CRYPTO_MODE_HMAC_MD5]          = 242,
	[CRYPTO_MODE_HASH_SHA1]         = 242,
	[CRYPTO_MODE_HMAC_SHA1]         = 242,
	[CRYPTO_MODE_HASH_SHA224]       = 242,
	[CRYPTO_MODE_HMAC_SHA224]       = 242,
	[CRYPTO_MODE_HASH_SHA256]       = 242,
	[CRYPTO_MODE_HMAC_SHA256]       = 242,
	[CRYPTO_MODE_HASH_SHA384]       = 242,
	[CRYPTO_MODE_HMAC_SHA384]       = 242,
	[CRYPTO_MODE_HASH_SHA512]       = 242,
	[CRYPTO_MODE_HMAC_SHA512]       = 242,
	[CRYPTO_MODE_HASH_SHA512_224]   = 242,
	[CRYPTO_MODE_HMAC_SHA512_224]   = 242,
	[CRYPTO_MODE_HASH_SHA512_256]   = 242,
	[CRYPTO_MODE_HMAC_SHA512_256]   = 242,
	[CRYPTO_MODE_MAC_XCBC]          = 154,	/* XaCBC */
	[CRYPTO_MODE_MAC_CMAC]          = 154,	/* CMAC */
	[CRYPTO_MODE_MAC_KASUMI_F9]     = 130,	/* KASUMI */
	[CRYPTO_MODE_MAC_SNOW3G_UIA2]   = 130,	/* SNOW */
	[CRYPTO_MODE_MAC_ZUC_UIA3]      = 130,	/* ZUC */
	[CRYPTO_MODE_MAC_POLY1305]      = 144,
	[CRYPTO_MODE_SSLMAC_MD5]        = 130,
	[CRYPTO_MODE_SSLMAC_SHA1]       = 132,
	[CRYPTO_MODE_HASH_CRC32]        = 0,
	[CRYPTO_MODE_MAC_MICHAEL]       = 129,

	[CRYPTO_MODE_HASH_SHA3_224]     = 242,
	[CRYPTO_MODE_HASH_SHA3_256]     = 242,
	[CRYPTO_MODE_HASH_SHA3_384]     = 242,
	[CRYPTO_MODE_HASH_SHA3_512]     = 242,
	[CRYPTO_MODE_HASH_SHAKE128]     = 242,
	[CRYPTO_MODE_HASH_SHAKE256]     = 242,
	[CRYPTO_MODE_HASH_CSHAKE128]    = 130,
	[CRYPTO_MODE_HASH_CSHAKE256]    = 130,
	[CRYPTO_MODE_MAC_KMAC128]       = 242,
	[CRYPTO_MODE_MAC_KMAC256]       = 242,
	[CRYPTO_MODE_MAC_KMACXOF128]    = 242,
	[CRYPTO_MODE_MAC_KMACXOF256]    = 242,
	[CRYPTO_MODE_HASH_SM3]          = 242,
	[CRYPTO_MODE_HMAC_SM3]          = 242,
	[CRYPTO_MODE_MAC_SM4_XCBC]      = 242,
	[CRYPTO_MODE_MAC_SM4_CMAC]      = 242,
};

#if IS_ENABLED(CONFIG_CRYPTO_DEV_SPACC_AUTODETECT)
static const struct {
	unsigned int min_version;
	struct {
		int outlen;
		unsigned char data[64];
	} test[7];
} testdata[CRYPTO_MODE_LAST] = {
	/* NULL*/
	{ .min_version = 0x65,
		.test[0].outlen = 0
	},

	/* AES_ECB*/
	{  .min_version = 0x65,
		.test[2].outlen = 16, .test[2].data = { 0xc6, 0xa1, 0x3b, 0x37,
			0x87, 0x8f, 0x5b, 0x82, 0x6f, 0x4f, 0x81, 0x62, 0xa1,
			0xc8, 0xd8, 0x79,  },
		.test[3].outlen = 16, .test[3].data = { 0x91, 0x62, 0x51, 0x82,
			0x1c, 0x73, 0xa5, 0x22, 0xc3, 0x96, 0xd6, 0x27, 0x38,
			0x01, 0x96, 0x07,  },
		.test[4].outlen = 16, .test[4].data = { 0xf2, 0x90, 0x00, 0xb6,
			0x2a, 0x49, 0x9f, 0xd0, 0xa9, 0xf3, 0x9a, 0x6a, 0xdd,
			0x2e, 0x77, 0x80,  },
	},

	/* AES_CBC*/
	{  .min_version = 0x65,
		.test[2].outlen = 16, .test[2].data = { 0x0a, 0x94, 0x0b, 0xb5,
			0x41, 0x6e, 0xf0, 0x45, 0xf1, 0xc3, 0x94, 0x58, 0xc6,
			0x53, 0xea, 0x5a,  },
		.test[3].outlen = 16, .test[3].data = { 0x00, 0x60, 0xbf, 0xfe,
			0x46, 0x83, 0x4b, 0xb8, 0xda, 0x5c, 0xf9, 0xa6, 0x1f,
			0xf2, 0x20, 0xae,  },
		.test[4].outlen = 16, .test[4].data = { 0x5a, 0x6e, 0x04, 0x57,
			0x08, 0xfb, 0x71, 0x96, 0xf0, 0x2e, 0x55, 0x3d, 0x02,
			0xc3, 0xa6, 0x92,  },
	},

	/* AES_CTR*/
	{  .min_version = 0x65,
		.test[2].outlen = 16, .test[2].data = { 0x0a, 0x94, 0x0b, 0xb5,
			0x41, 0x6e, 0xf0, 0x45, 0xf1, 0xc3, 0x94, 0x58, 0xc6,
			0x53, 0xea, 0x5a,  },
		.test[3].outlen = 16, .test[3].data = { 0x00, 0x60, 0xbf, 0xfe,
			0x46, 0x83, 0x4b, 0xb8, 0xda, 0x5c, 0xf9, 0xa6, 0x1f,
			0xf2, 0x20, 0xae,  },
		.test[4].outlen = 16, .test[4].data = { 0x5a, 0x6e, 0x04, 0x57,
			0x08, 0xfb, 0x71, 0x96, 0xf0, 0x2e, 0x55, 0x3d, 0x02,
			0xc3, 0xa6, 0x92,  },
	},

	/* AES_CCM*/
	{  .min_version = 0x65,
		.test[2].outlen = 32, .test[2].data = { 0x02, 0x63, 0xec, 0x94,
			0x66, 0x18, 0x72, 0x96, 0x9a, 0xda, 0xfd, 0x0f, 0x4b,
			0xa4, 0x0f, 0xdc, 0xa5, 0x09, 0x92, 0x93, 0xb6, 0xb4,
			0x38, 0x34, 0x63, 0x72, 0x50, 0x4c, 0xfc, 0x8a, 0x63,
			0x02,  },
		.test[3].outlen = 32, .test[3].data = { 0x29, 0xf7, 0x63, 0xe8,
			0xa1, 0x75, 0xc6, 0xbf, 0xa5, 0x54, 0x94, 0x89, 0x12,
			0x84, 0x45, 0xf5, 0x9b, 0x27, 0xeb, 0xb1, 0xa4, 0x65,
			0x93, 0x6e, 0x5a, 0xc0, 0xa2, 0xa3, 0xe2, 0x6c, 0x46,
			0x29,  },
		.test[4].outlen = 32, .test[4].data = { 0x60, 0xf3, 0x10, 0xd5,
			0xc3, 0x85, 0x58, 0x5d, 0x55, 0x16, 0xfb, 0x51, 0x72,
			0xe5, 0x20, 0xcf, 0x8e, 0x87, 0x6d, 0x72, 0xc8, 0x44,
			0xbe, 0x6d, 0xa2, 0xd6, 0xf4, 0xba, 0xec, 0xb4, 0xec,
			0x39,  },
	},

	/* AES_GCM*/
	{  .min_version = 0x65,
		.test[2].outlen = 32, .test[2].data = { 0x93, 0x6c, 0xa7, 0xce,
			0x66, 0x1b, 0xf7, 0x54, 0x4b, 0xd2, 0x61, 0x8a, 0x36,
			0xa3, 0x70, 0x08, 0xc0, 0xd7, 0xd0, 0x77, 0xc5, 0x64,
			0x76, 0xdb, 0x48, 0x4a, 0x53, 0xe3, 0x6c, 0x93, 0x34,
			0x0f,  },
		.test[3].outlen = 32, .test[3].data = { 0xe6, 0xf9, 0x22, 0x9b,
			0x99, 0xb9, 0xc9, 0x0e, 0xd0, 0x33, 0xdc, 0x82, 0xff,
			0xa9, 0xdc, 0x70, 0x4c, 0xcd, 0xc4, 0x1b, 0xa3, 0x5a,
			0x87, 0x5d, 0xd8, 0xef, 0xb6, 0x48, 0xbb, 0x0c, 0x92,
			0x60,  },
		.test[4].outlen = 32, .test[4].data = { 0x47, 0x02, 0xd6, 0x1b,
			0xc5, 0xe5, 0xc2, 0x1b, 0x8d, 0x41, 0x97, 0x8b, 0xb1,
			0xe9, 0x78, 0x6d, 0x48, 0x6f, 0x78, 0x81, 0xc7, 0x98,
			0xcc, 0xf5, 0x28, 0xf1, 0x01, 0x7c, 0xe8, 0xf6, 0x09,
			0x78,  },
	},

	/* AES-F8*/
	{  .min_version = 0x65,
		.test[0].outlen = 0
	},

	/* AES-XTS*/
	{  .min_version = 0x65,
		.test[2].outlen = 32, .test[2].data = { 0xa0, 0x1a, 0x6f, 0x09,
			0xfa, 0xef, 0xd2, 0x72, 0xc3, 0x9b, 0xad, 0x35, 0x52,
			0xfc, 0xa1, 0xcb, 0x33, 0x69, 0x51, 0xc5, 0x23, 0xbe,
			0xac, 0xa5, 0x4a, 0xf2, 0xfc, 0x77, 0x71, 0x6f, 0x9a,
			0x86,  },
		.test[4].outlen = 32, .test[4].data = { 0x05, 0x45, 0x91, 0x86,
			0xf2, 0x2d, 0x97, 0x93, 0xf3, 0xa0, 0xbb, 0x29, 0xc7,
			0x9c, 0xc1, 0x4c, 0x3b, 0x8f, 0xdd, 0x9d, 0xda, 0xc7,
			0xb5, 0xaa, 0xc2, 0x7c, 0x2e, 0x71, 0xce, 0x7f, 0xce,
			0x0e,  },
	},

	/* AES-CFB*/
	{  .min_version = 0x65,
		.test[0].outlen = 0
	},

	/* AES-OFB*/
	{  .min_version = 0x65,
		.test[0].outlen = 0
	},

	/* AES-CS1*/
	{  .min_version = 0x65,
		.test[2].outlen = 31, .test[2].data = { 0x0a, 0x94, 0x0b, 0xb5,
			0x41, 0x6e, 0xf0, 0x45, 0xf1, 0xc3, 0x94, 0x58, 0xc6,
			0x53, 0xea, 0xae, 0xe7, 0x1e, 0xa5, 0x41, 0xd7, 0xae,
			0x4b, 0xeb, 0x60, 0xbe, 0xcc, 0x59, 0x3f, 0xb6, 0x63,
		},
		.test[3].outlen = 31, .test[3].data = { 0x00, 0x60, 0xbf, 0xfe,
			0x46, 0x83, 0x4b, 0xb8, 0xda, 0x5c, 0xf9, 0xa6, 0x1f,
			0xf2, 0x20, 0x2e, 0x84, 0xcb, 0x12, 0xa3, 0x59, 0x17,
			0xb0, 0x9e, 0x25, 0xa2, 0xa2, 0x3d, 0xf1, 0x9f, 0xdc,
		},
		.test[4].outlen = 31, .test[4].data = { 0x5a, 0x6e, 0x04, 0x57,
			0x08, 0xfb, 0x71, 0x96, 0xf0, 0x2e, 0x55, 0x3d, 0x02,
			0xc3, 0xa6, 0xcd, 0xfc, 0x25, 0x35, 0x31, 0x0b, 0xf5,
			0x6b, 0x2e, 0xb7, 0x8a, 0xa2, 0x5a, 0xdd, 0x77, 0x51,
		},
	},

	/* AES-CS2*/
	{  .min_version = 0x65,
		.test[2].outlen = 31, .test[2].data = { 0xae, 0xe7, 0x1e, 0xa5,
			0x41, 0xd7, 0xae, 0x4b, 0xeb, 0x60, 0xbe, 0xcc, 0x59,
			0x3f, 0xb6, 0x63, 0x0a, 0x94, 0x0b, 0xb5, 0x41, 0x6e,
			0xf0, 0x45, 0xf1, 0xc3, 0x94, 0x58, 0xc6, 0x53, 0xea,
		},
		.test[3].outlen = 31, .test[3].data = { 0x2e, 0x84, 0xcb, 0x12,
			0xa3, 0x59, 0x17, 0xb0, 0x9e, 0x25, 0xa2, 0xa2, 0x3d,
			0xf1, 0x9f, 0xdc, 0x00, 0x60, 0xbf, 0xfe, 0x46, 0x83,
			0x4b, 0xb8, 0xda, 0x5c, 0xf9, 0xa6, 0x1f, 0xf2, 0x20,
		},
		.test[4].outlen = 31, .test[4].data = { 0xcd, 0xfc, 0x25, 0x35,
			0x31, 0x0b, 0xf5, 0x6b, 0x2e, 0xb7, 0x8a, 0xa2, 0x5a,
			0xdd, 0x77, 0x51, 0x5a, 0x6e, 0x04, 0x57, 0x08, 0xfb,
			0x71, 0x96, 0xf0, 0x2e, 0x55, 0x3d, 0x02, 0xc3, 0xa6,
		},
	},

	/* AES-CS3*/
	{  .min_version = 0x65,
		.test[2].outlen = 31, .test[2].data = { 0xae, 0xe7, 0x1e, 0xa5,
			0x41, 0xd7, 0xae, 0x4b, 0xeb, 0x60, 0xbe, 0xcc, 0x59,
			0x3f, 0xb6, 0x63, 0x0a, 0x94, 0x0b, 0xb5, 0x41, 0x6e,
			0xf0, 0x45, 0xf1, 0xc3, 0x94, 0x58, 0xc6, 0x53, 0xea,
		},
		.test[3].outlen = 31, .test[3].data = { 0x2e, 0x84, 0xcb, 0x12,
			0xa3, 0x59, 0x17, 0xb0, 0x9e, 0x25, 0xa2, 0xa2, 0x3d,
			0xf1, 0x9f, 0xdc, 0x00, 0x60, 0xbf, 0xfe, 0x46, 0x83,
			0x4b, 0xb8, 0xda, 0x5c, 0xf9, 0xa6, 0x1f, 0xf2, 0x20,
		},
		.test[4].outlen = 31, .test[4].data = { 0xcd, 0xfc, 0x25, 0x35,
			0x31, 0x0b, 0xf5, 0x6b, 0x2e, 0xb7, 0x8a, 0xa2, 0x5a,
			0xdd, 0x77, 0x51, 0x5a, 0x6e, 0x04, 0x57, 0x08, 0xfb,
			0x71, 0x96, 0xf0, 0x2e, 0x55, 0x3d, 0x02, 0xc3, 0xa6,
		},
	},

	/* MULTI2*/
	{  .min_version = 0x65,
		.test[0].outlen = 0
	},
	{  .min_version = 0x65,
		.test[0].outlen = 0
	},
	{  .min_version = 0x65,
		.test[0].outlen = 0
	},
	{  .min_version = 0x65,
		.test[0].outlen = 0
	},

	/* 3DES_CBC*/
	{  .min_version = 0x65,
		.test[3].outlen = 16, .test[3].data = { 0x58, 0xed, 0x24, 0x8f,
			0x77, 0xf6, 0xb1, 0x9e, 0x47, 0xd9, 0xb7, 0x4a, 0x4f,
			0x5a, 0xe6, 0x6d,  }
	},

	/* 3DES_ECB*/
	{  .min_version = 0x65,
		.test[3].outlen = 16, .test[3].data = { 0x89, 0x4b, 0xc3, 0x08,
			0x54, 0x26, 0xa4, 0x41, 0x89, 0x4b, 0xc3, 0x08, 0x54,
			0x26, 0xa4, 0x41,  }
	},

	/* DES_CBC*/
	{  .min_version = 0x65,
		.test[1].outlen = 16, .test[1].data = { 0xe1, 0xb2, 0x46, 0xe5,
			0xa7, 0xc7, 0x4c, 0xbc, 0xd5, 0xf0, 0x8e, 0x25, 0x3b,
			0xfa, 0x23, 0x80,  }
	},

	/* DES_ECB*/
	{  .min_version = 0x65,
		.test[1].outlen = 16, .test[1].data =  { 0xa5, 0x17, 0x3a,
			0xd5, 0x95, 0x7b, 0x43, 0x70, 0xa5, 0x17, 0x3a, 0xd5,
			0x95, 0x7b, 0x43, 0x70,  }
	},

	/* KASUMI_ECB*/
	{  .min_version = 0x65,
		.test[2].outlen = 16, .test[2].data =  { 0x04, 0x7d, 0x5d,
			0x2c, 0x8c, 0x2e, 0x91, 0xb3, 0x04, 0x7d, 0x5d, 0x2c,
			0x8c, 0x2e, 0x91, 0xb3,  } },

	/* KASUMI_F8*/
	{  .min_version = 0x65,
		.test[2].outlen = 16, .test[2].data =  { 0xfc, 0xf7, 0x45,
			0xee, 0x1d, 0xbb, 0xa4, 0x57, 0xa7, 0x45, 0xdc, 0x6b,
			0x2a, 0x1b, 0x50, 0x88,  }
	},

	/* SNOW3G UEA2*/
	{  .min_version = 0x65,
		.test[2].outlen = 16, .test[2].data =  { 0x95, 0xd3, 0xc8,
			0x13, 0xc0, 0x20, 0x24, 0xa3, 0x76, 0x24, 0xd1, 0x98,
			0xb6, 0x67, 0x4d, 0x4c,  }
	},

	/* ZUC UEA3*/
	{  .min_version = 0x65,
		.test[2].outlen = 16, .test[2].data =  { 0xda, 0xdf, 0xb6,
			0xa2, 0xac, 0x9d, 0xba, 0xfe, 0x18, 0x9c, 0x0c, 0x75,
			0x79, 0xc6, 0xe0, 0x4e,  }
	},

	/* CHACHA20_STREAM*/
	{  .min_version = 0x65,
		.test[4].outlen = 16, .test[4].data =  { 0x55, 0xdf, 0x91,
			0xe9, 0x27, 0x01, 0x37, 0x69, 0xdb, 0x38, 0xd4, 0x28,
			0x01, 0x79, 0x76, 0x64 }
	},

	/* CHACHA20_POLY1305 (AEAD)*/
	{  .min_version = 0x65,
		.test[4].outlen = 16, .test[4].data =  { 0x89, 0xfb, 0x08,
			0x00, 0x29, 0x17, 0xa5, 0x40, 0xb7, 0x83, 0x3f, 0xf3,
			0x98, 0x1d, 0x0e, 0x63 }
	},

	/* SM4_ECB 128*/
	{  .min_version = 0x65,
		.test[2].outlen = 16, .test[2].data =  { 0x1e, 0x96, 0x34,
			0xb7, 0x70, 0xf9, 0xae, 0xba, 0xa9, 0x34, 0x4f, 0x5a,
			0xff, 0x9f, 0x82, 0xa3 }
	},

	/* SM4_CBC 128*/
	{  .min_version = 0x65,
		.test[2].outlen = 16, .test[2].data =  { 0x8f, 0x78, 0x76,
			0x3e, 0xe0, 0x60, 0x13, 0xe0, 0xb7, 0x62, 0x2c, 0x42,
			0x8f, 0xd0, 0x52, 0x8d }
	},

	/* SM4_CFB 128*/
	{  .min_version = 0x65,
		.test[2].outlen = 16, .test[2].data =  { 0x8f, 0x78, 0x76,
			0x3e, 0xe0, 0x60, 0x13, 0xe0, 0xb7, 0x62, 0x2c, 0x42,
			0x8f, 0xd0, 0x52, 0x8d }
	},

	 /* SM4_OFB 128*/
	 {  .min_version = 0x65,
	 .test[2].outlen = 16, .test[2].data =  { 0x8f, 0x78, 0x76, 0x3e, 0xe0,
		 0x60, 0x13, 0xe0, 0xb7, 0x62, 0x2c, 0x42, 0x8f, 0xd0, 0x52,
		 0x8d }
	 },

	 /* SM4_CTR 128*/
	 {  .min_version = 0x65,
	 .test[2].outlen = 16, .test[2].data =  { 0x8f, 0x78, 0x76, 0x3e, 0xe0,
		 0x60, 0x13, 0xe0, 0xb7, 0x62, 0x2c, 0x42, 0x8f, 0xd0, 0x52,
		 0x8d }
	 },

	/* SM4_CCM 128*/
	{  .min_version = 0x65,
		.test[2].outlen = 16, .test[2].data =  { 0x8e, 0x25, 0x5a,
			0x13, 0xc7, 0x43, 0x4d, 0x95, 0xef, 0x14, 0x15, 0x11,
			0xd0, 0xb9, 0x60, 0x5b }
	},

	/* SM4_GCM 128*/
	{  .min_version = 0x65,
		.test[2].outlen = 16, .test[2].data =  { 0x97, 0x46, 0xde,
			0xfb, 0xc9, 0x6a, 0x85, 0x00, 0xff, 0x9c, 0x74, 0x4d,
			0xd1, 0xbb, 0xf9, 0x66 }
	},

	/* SM4_F8 128*/
	{  .min_version = 0x65,
		.test[2].outlen = 16, .test[2].data =  { 0x77, 0x30, 0xff,
			0x70, 0x46, 0xbc, 0xf4, 0xe3, 0x11, 0xf6, 0x27, 0xe2,
			0xff, 0xd7, 0xc4, 0x2e }
	},

	/* SM4_XTS 128*/
	{  .min_version = 0x65,
		.test[2].outlen = 16, .test[2].data =  { 0x05, 0x3f, 0xb6,
			0xe9, 0xb1, 0xff, 0x09, 0x4f, 0x9d, 0x69, 0x4d, 0xc2,
			0xb6, 0xa1, 0x15, 0xde }
	},

	/* SM4_CS1 128*/
	{  .min_version = 0x65,
		.test[2].outlen = 16, .test[2].data =  { 0x8f, 0x78, 0x76,
			0x3e, 0xe0, 0x60, 0x13, 0xe0, 0xb7, 0x62, 0x2c, 0x42,
			0x8f, 0xd0, 0x52, 0xa0 }
	},

	/* SM4_CS2 128*/
	{  .min_version = 0x65,
		.test[2].outlen = 16, .test[2].data =  { 0xa0, 0x1c, 0xfe,
			0x91, 0xaa, 0x7e, 0xf1, 0x75, 0x6a, 0xe8, 0xbc, 0xe1,
			0x55, 0x08, 0xda, 0x71 }
	},

	/* SM4_CS3 128*/
	{  .min_version = 0x65,
		.test[2].outlen = 16, .test[2].data =  { 0xa0, 0x1c, 0xfe,
			0x91, 0xaa, 0x7e, 0xf1, 0x75, 0x6a, 0xe8, 0xbc, 0xe1,
			0x55, 0x08, 0xda, 0x71 }
	},

	/* hashes ... note they use the 2nd keysize
	 * array so the indecies mean different sizes!!!
	 */

	/* MD5 HASH/HMAC*/
	{  .min_version = 0x65,
		.test[1].outlen = 16, .test[1].data = { 0x70, 0xbc, 0x8f, 0x4b,
			0x72, 0xa8, 0x69, 0x21, 0x46, 0x8b, 0xf8, 0xe8, 0x44,
			0x1d, 0xce, 0x51,  }
	},
	{  .min_version = 0x65,
		.test[1].outlen = 16, .test[1].data = { 0xb6, 0x39, 0xc8, 0x73,
			0x16, 0x38, 0x61, 0x8b, 0x70, 0x79, 0x72, 0xaa, 0x6e,
			0x96, 0xcf, 0x90,  },
		.test[4].outlen = 16, .test[4].data = { 0xb7, 0x79, 0x68, 0xea,
			0x17, 0x32, 0x1e, 0x32, 0x13, 0x90, 0x6c, 0x2e, 0x9f,
			0xd5, 0xc8, 0xb3,  },
		.test[5].outlen = 16, .test[5].data = { 0x80, 0x3e, 0x0a, 0x2f,
			0x8a, 0xd8, 0x31, 0x8f, 0x8e, 0x12, 0x28, 0x86, 0x22,
			0x59, 0x6b, 0x05,  },
	},
	/* SHA1*/
	{  .min_version = 0x65,
		.test[1].outlen = 20, .test[1].data = { 0xde, 0x8a, 0x84, 0x7b,
			0xff, 0x8c, 0x34, 0x3d, 0x69, 0xb8, 0x53, 0xa2, 0x15,
			0xe6, 0xee, 0x77, 0x5e, 0xf2, 0xef, 0x96,  }
	},
	{  .min_version = 0x65,
		.test[1].outlen = 20, .test[1].data = { 0xf8, 0x54, 0x60, 0x50,
			0x49, 0x56, 0xd1, 0xcd, 0x55, 0x5c, 0x5d, 0xcd, 0x24,
			0x33, 0xbf, 0xdc, 0x5c, 0x99, 0x54, 0xc8,  },
		.test[4].outlen = 20, .test[4].data = { 0x66, 0x3f, 0x3a, 0x3c,
			0x08, 0xb6, 0x87, 0xb2, 0xd3, 0x0c, 0x5a, 0xa7, 0xcc,
			0x5c, 0xc3, 0x99, 0xb2, 0xb4, 0x58, 0x55,  },
		.test[5].outlen = 20, .test[5].data = { 0x9a, 0x28, 0x54, 0x2f,
			0xaf, 0xa7, 0x0b, 0x37, 0xbe, 0x2d, 0x3e, 0xd9, 0xd4,
			0x70, 0xbc, 0xdc, 0x0b, 0x54, 0x20, 0x06,  },
	},
	/* SHA224_HASH*/
	{  .min_version = 0x65,
		.test[1].outlen = 28, .test[1].data = { 0xb3, 0x38, 0xc7, 0x6b,
			0xcf, 0xfa, 0x1a, 0x0b, 0x3e, 0xad, 0x8d, 0xe5, 0x8d,
			0xfb, 0xff, 0x47, 0xb6, 0x3a, 0xb1, 0x15, 0x0e, 0x10,
			0xd8, 0xf1, 0x7f, 0x2b, 0xaf, 0xdf,  }
	},
	{  .min_version = 0x65,
		.test[1].outlen = 28, .test[1].data = { 0xf3, 0xb4, 0x33, 0x78,
			0x53, 0x4c, 0x0c, 0x4a, 0x1e, 0x31, 0xc2, 0xce, 0xda,
			0xc8, 0xfe, 0x74, 0x4a, 0xd2, 0x9b, 0x7c, 0x1d, 0x2f,
			0x5e, 0xa1, 0xaa, 0x31, 0xb9, 0xf5,  },
		.test[4].outlen = 28, .test[4].data = { 0x4b, 0x6b, 0x3f, 0x9a,
			0x66, 0x47, 0x45, 0xe2, 0x60, 0xc9, 0x53, 0x86, 0x7a,
			0x34, 0x65, 0x7d, 0xe2, 0x24, 0x06, 0xcc, 0xf9, 0x17,
			0x20, 0x5d, 0xc2, 0xb6, 0x97, 0x9a,  },
		.test[5].outlen = 28, .test[5].data = { 0x90, 0xb0, 0x6e, 0xee,
			0x21, 0x57, 0x38, 0xc7, 0x65, 0xbb, 0x9a, 0xf5, 0xb4,
			0x31, 0x0a, 0x0e, 0xe5, 0x64, 0xc4, 0x49, 0x9d, 0xbd,
			0xe9, 0xf7, 0xac, 0x9f, 0xf8, 0x05,  },
	},

	/* SHA256_HASH*/
	{  .min_version = 0x65,
		.test[1].outlen = 32, .test[1].data = { 0x66, 0x68, 0x7a, 0xad,
			0xf8, 0x62, 0xbd, 0x77, 0x6c, 0x8f, 0xc1, 0x8b, 0x8e,
			0x9f, 0x8e, 0x20, 0x08, 0x97, 0x14, 0x85, 0x6e, 0xe2,
			0x33, 0xb3, 0x90, 0x2a, 0x59, 0x1d, 0x0d, 0x5f, 0x29,
			0x25,  }
	},
	{  .min_version = 0x65,
		.test[1].outlen = 32, .test[1].data = { 0x75, 0x40, 0x84, 0x49,
			0x54, 0x0a, 0xf9, 0x80, 0x99, 0xeb, 0x93, 0x6b, 0xf6,
			0xd3, 0xff, 0x41, 0x05, 0x47, 0xcc, 0x82, 0x62, 0x76,
			0x32, 0xf3, 0x43, 0x74, 0x70, 0x54, 0xe2, 0x3b, 0xc0,
			0x90,  },
		.test[4].outlen = 32, .test[4].data = { 0x41, 0x6c, 0x53, 0x92,
			0xb9, 0xf3, 0x6d, 0xf1, 0x88, 0xe9, 0x0e, 0xb1, 0x4d,
			0x17, 0xbf, 0x0d, 0xa1, 0x90, 0xbf, 0xdb, 0x7f, 0x1f,
			0x49, 0x56, 0xe6, 0xe5, 0x66, 0xa5, 0x69, 0xc8, 0xb1,
			0x5c,  },
		.test[5].outlen = 32, .test[5].data = { 0x49, 0x1f, 0x58, 0x3b,
			0x05, 0xe2, 0x3a, 0x72, 0x1d, 0x11, 0x6d, 0xc1, 0x08,
			0xa0, 0x3f, 0x30, 0x37, 0x98, 0x36, 0x8a, 0x49, 0x4c,
			0x21, 0x1d, 0x56, 0xa5, 0x2a, 0xf3, 0x68, 0x28, 0xb7,
			0x69,  },
	},
	/* SHA384_HASH*/
	{  .min_version = 0x65,
		.test[1].outlen = 48, .test[1].data = { 0xa3, 0x8f, 0xff, 0x4b,
			0xa2, 0x6c, 0x15, 0xe4, 0xac, 0x9c, 0xde, 0x8c, 0x03,
			0x10, 0x3a, 0xc8, 0x90, 0x80, 0xfd, 0x47, 0x54, 0x5f,
			0xde, 0x94, 0x46, 0xc8, 0xf1, 0x92, 0x72, 0x9e, 0xab,
			0x7b, 0xd0, 0x3a, 0x4d, 0x5c, 0x31, 0x87, 0xf7, 0x5f,
			0xe2, 0xa7, 0x1b, 0x0e, 0xe5, 0x0a, 0x4a, 0x40,  }
	},
	{  .min_version = 0x65,
		.test[1].outlen = 48, .test[1].data = { 0x6c, 0xd8, 0x89, 0xa0,
			0xca, 0x54, 0xa6, 0x1d, 0x24, 0xc4, 0x1d, 0xa1, 0x77,
			0x50, 0xd6, 0xf2, 0xf3, 0x43, 0x23, 0x0d, 0xb1, 0xf5,
			0xf7, 0xfc, 0xc0, 0x8c, 0xf6, 0xdf, 0x3c, 0x61, 0xfc,
			0x8a, 0xb9, 0xda, 0x12, 0x75, 0x97, 0xac, 0x51, 0x88,
			0x59, 0x19, 0x44, 0x13, 0xc0, 0x78, 0xa5, 0xa8,  },
		.test[4].outlen = 48, .test[4].data = { 0x0c, 0x91, 0x36, 0x46,
			0xd9, 0x17, 0x81, 0x46, 0x1d, 0x42, 0xb1, 0x00, 0xaa,
			0xfa, 0x26, 0x92, 0x9f, 0x05, 0xc0, 0x91, 0x8e, 0x20,
			0xd7, 0x75, 0x9d, 0xd2, 0xc8, 0x9b, 0x02, 0x18, 0x20,
			0x1f, 0xdd, 0xa3, 0x32, 0xe3, 0x1e, 0xa4, 0x2b, 0xc3,
			0xc8, 0xb9, 0xb1, 0x53, 0x4e, 0x6a, 0x49, 0xd2,  },
		.test[5].outlen = 48, .test[5].data = { 0x84, 0x78, 0xd2, 0xf1,
			0x44, 0x95, 0x6a, 0x22, 0x2d, 0x08, 0x19, 0xe8, 0xea,
			0x61, 0xb4, 0x86, 0xe8, 0xc6, 0xb0, 0x40, 0x51, 0x28,
			0x22, 0x54, 0x48, 0xc0, 0x70, 0x09, 0x81, 0xf9, 0xf5,
			0x47, 0x9e, 0xb3, 0x2c, 0x69, 0x19, 0xd5, 0x8d, 0x03,
			0x5d, 0x24, 0xca, 0x90, 0xa6, 0x9d, 0x80, 0x2a,  },
		.test[6].outlen = 48, .test[6].data = { 0x0e, 0x68, 0x17, 0x31,
			0x01, 0xa8, 0x28, 0x0a, 0x4e, 0x47, 0x22, 0xa6, 0x89,
			0xf0, 0xc6, 0xcd, 0x4e, 0x8c, 0x19, 0x4c, 0x44, 0x3d,
			0xb5, 0xa5, 0xf9, 0xfe, 0xea, 0xc7, 0x84, 0x0b, 0x57,
			0x0d, 0xd4, 0xe4, 0x8a, 0x3f, 0x68, 0x31, 0x20, 0xd9,
			0x1f, 0xc4, 0xa3, 0x76, 0xcf, 0xdd, 0x07, 0xa6,  },
	},
	/* SHA512_HASH */
	{  .min_version = 0x65,
		.test[1].outlen = 64, .test[1].data = { 0x50, 0x46, 0xad, 0xc1,
			0xdb, 0xa8, 0x38, 0x86, 0x7b, 0x2b, 0xbb, 0xfd, 0xd0,
			0xc3, 0x42, 0x3e, 0x58, 0xb5, 0x79, 0x70, 0xb5, 0x26,
			0x7a, 0x90, 0xf5, 0x79, 0x60, 0x92, 0x4a, 0x87, 0xf1,
			0x96, 0x0a, 0x6a, 0x85, 0xea, 0xa6, 0x42, 0xda, 0xc8,
			0x35, 0x42, 0x4b, 0x5d, 0x7c, 0x8d, 0x63, 0x7c, 0x00,
			0x40, 0x8c, 0x7a, 0x73, 0xda, 0x67, 0x2b, 0x7f, 0x49,
			0x85, 0x21, 0x42, 0x0b, 0x6d, 0xd3,  }
	},
	{  .min_version = 0x65,
		.test[1].outlen = 64, .test[1].data = { 0xec, 0xfd, 0x83, 0x74,
			0xc8, 0xa9, 0x2f, 0xd7, 0x71, 0x94, 0xd1, 0x1e, 0xe7,
			0x0f, 0x0f, 0x5e, 0x11, 0x29, 0x58, 0xb8, 0x36, 0xc6,
			0x39, 0xbc, 0xd6, 0x88, 0x6e, 0xdb, 0xc8, 0x06, 0x09,
			0x30, 0x27, 0xaa, 0x69, 0xb9, 0x2a, 0xd4, 0x67, 0x06,
			0x5c, 0x82, 0x8e, 0x90, 0xe9, 0x3e, 0x55, 0x88, 0x7d,
			0xb2, 0x2b, 0x48, 0xa2, 0x28, 0x92, 0x6c, 0x0f, 0xf1,
			0x57, 0xb5, 0xd0, 0x06, 0x1d, 0xf3,  },
		.test[4].outlen = 64, .test[4].data = { 0x47, 0x88, 0x91, 0xe9,
			0x12, 0x3e, 0xfd, 0xdc, 0x26, 0x29, 0x08, 0xd6, 0x30,
			0x8f, 0xcc, 0xb6, 0x93, 0x30, 0x58, 0x69, 0x4e, 0x81,
			0xee, 0x9d, 0xb6, 0x0f, 0xc5, 0x54, 0xe6, 0x7c, 0x84,
			0xc5, 0xbc, 0x89, 0x99, 0xf0, 0xf3, 0x7f, 0x6f, 0x3f,
			0xf5, 0x04, 0x2c, 0xdf, 0x76, 0x72, 0x6a, 0xbe, 0x28,
			0x3b, 0xb8, 0x05, 0xb3, 0x47, 0x45, 0xf5, 0x7f, 0xb1,
			0x21, 0x2d, 0xe0, 0x8d, 0x1e, 0x29,  },
		.test[5].outlen = 64, .test[5].data = { 0x7e, 0x55, 0xda, 0x88,
			0x28, 0xc1, 0x6e, 0x9a, 0x6a, 0x99, 0xa0, 0x37, 0x68,
			0xf0, 0x28, 0x5e, 0xe2, 0xbe, 0x00, 0xac, 0x76, 0x89,
			0x76, 0xcc, 0x5d, 0x98, 0x1b, 0x32, 0x1a, 0x14, 0xc4,
			0x2e, 0x9c, 0xe4, 0xf3, 0x3f, 0x5f, 0xa0, 0xae, 0x95,
			0x16, 0x0b, 0x14, 0xf5, 0xf5, 0x45, 0x29, 0xd8, 0xc9,
			0x43, 0xf2, 0xa9, 0xbc, 0xdc, 0x03, 0x81, 0x0d, 0x36,
			0x2f, 0xb1, 0x22, 0xe8, 0x13, 0xf8,  },
		.test[6].outlen = 64, .test[6].data = { 0x5d, 0xc4, 0x80, 0x90,
			0x6b, 0x00, 0x17, 0x04, 0x34, 0x63, 0x93, 0xf1, 0xad,
			0x9a, 0x3e, 0x13, 0x37, 0x6b, 0x86, 0xd7, 0xc4, 0x2b,
			0x22, 0x9c, 0x2e, 0xf2, 0x1d, 0xde, 0x35, 0x39, 0x03,
			0x3f, 0x2b, 0x3a, 0xc3, 0x49, 0xb3, 0x32, 0x86, 0x63,
			0x6b, 0x0f, 0x27, 0x95, 0x97, 0xe5, 0xe7, 0x2b, 0x9b,
			0x80, 0xea, 0x94, 0x4d, 0x84, 0x2e, 0x39, 0x44, 0x8f,
			0x56, 0xe3, 0xcd, 0xa7, 0x12, 0x3e,  },
	},
	/* SHA512_224_HASH */
	{  .min_version = 0x65,
		.test[1].outlen = 28, .test[1].data = { 0x9e, 0x7d, 0x60, 0x80,
			0xde, 0xf4, 0xe1, 0xcc, 0xf4, 0xae, 0xaa, 0xc6, 0xf7,
			0xfa, 0xd0, 0x08, 0xd0, 0x60, 0xa6, 0xcf, 0x87, 0x06,
			0x20, 0x38, 0xd6, 0x16, 0x67, 0x74,  }
	},
	{  .min_version = 0x65,
		.test[1].outlen = 28, .test[1].data = { 0xff, 0xfb, 0x43, 0x27,
			0xdd, 0x2e, 0x39, 0xa0, 0x18, 0xa8, 0xaf, 0xde, 0x84,
			0x0b, 0x5d, 0x0f, 0x3d, 0xdc, 0xc6, 0x17, 0xd1, 0xb6,
			0x2f, 0x8c, 0xf8, 0x7e, 0x34, 0x34,  },
		.test[4].outlen = 28, .test[4].data = { 0x00, 0x19, 0xe2, 0x2d,
			0x44, 0x80, 0x2d, 0xd8, 0x1c, 0x57, 0xf5, 0x57, 0x92,
			0x08, 0x13, 0xe7, 0x9d, 0xbb, 0x2b, 0xc2, 0x8d, 0x77,
			0xc1, 0xff, 0x71, 0x4c, 0xf0, 0xa9,  },
		.test[5].outlen = 28, .test[5].data = { 0x6a, 0xc4, 0xa8, 0x73,
			0x21, 0x54, 0xb2, 0x82, 0xee, 0x89, 0x8d, 0x45, 0xd4,
			0xe3, 0x76, 0x3e, 0x04, 0x03, 0xc9, 0x71, 0xee, 0x01,
			0x25, 0xd2, 0x7b, 0xa1, 0x20, 0xc4,  },
		.test[6].outlen = 28, .test[6].data = { 0x0f, 0x98, 0x15, 0x9b,
			0x11, 0xca, 0x60, 0xc7, 0x82, 0x39, 0x1a, 0x50, 0x8c,
			0xe4, 0x79, 0xfa, 0xa8, 0x0e, 0xc7, 0x12, 0xfd, 0x8c,
			0x9c, 0x99, 0x7a, 0xe8, 0x7e, 0x92,  },
	},
	/* SHA512_256_HASH*/
	{  .min_version = 0x65,
		.test[1].outlen = 32, .test[1].data = { 0xaf, 0x13, 0xc0, 0x48,
			0x99, 0x12, 0x24, 0xa5, 0xe4, 0xc6, 0x64, 0x44, 0x6b,
			0x68, 0x8a, 0xaf, 0x48, 0xfb, 0x54, 0x56, 0xdb, 0x36,
			0x29, 0x60, 0x1b, 0x00, 0xec, 0x16, 0x0c, 0x74, 0xe5,
			0x54,  }
	},
	{  .min_version = 0x65,
		.test[1].outlen = 32, .test[1].data = { 0x3a, 0x2c, 0xd0, 0x2b,
			0xfa, 0xa6, 0x72, 0xe4, 0xf1, 0xab, 0x0a, 0x3e, 0x70,
			0xe4, 0x88, 0x1a, 0x92, 0xe1, 0x3b, 0x64, 0x5a, 0x9b,
			0xed, 0xb3, 0x97, 0xc0, 0x17, 0x1f, 0xd4, 0x05, 0xf1,
			0x72,  },
		.test[4].outlen = 32, .test[4].data = { 0x6f, 0x2d, 0xae, 0xc6,
			0xe4, 0xa6, 0x5b, 0x52, 0x0f, 0x26, 0x16, 0xf6, 0xa9,
			0xc1, 0x23, 0xc2, 0xb3, 0x67, 0xfc, 0x69, 0xac, 0x73,
			0x87, 0xa2, 0x5b, 0x6c, 0x44, 0xad, 0xc5, 0x26, 0x2b,
			0x10,  },
		.test[5].outlen = 32, .test[5].data = { 0x63, 0xe7, 0xb8, 0xd1,
			0x76, 0x33, 0x56, 0x29, 0xba, 0x99, 0x86, 0x42, 0x0d,
			0x4f, 0xf7, 0x54, 0x8c, 0xb9, 0x39, 0xf2, 0x72, 0x1d,
			0x0e, 0x9d, 0x80, 0x67, 0xd9, 0xab, 0x15, 0xb0, 0x68,
			0x18,  },
		.test[6].outlen = 32, .test[6].data = { 0x64, 0x78, 0x56, 0xd7,
			0xaf, 0x5b, 0x56, 0x08, 0xf1, 0x44, 0xf7, 0x4f, 0xa1,
			0xa1, 0x13, 0x79, 0x6c, 0xb1, 0x31, 0x11, 0xf3, 0x75,
			0xf4, 0x8c, 0xb4, 0x9f, 0xbf, 0xb1, 0x60, 0x38, 0x3d,
			0x28,  },
	},

	/* AESXCBC*/
	{  .min_version = 0x65,
		.test[1].outlen = 16, .test[1].data = { 0x35, 0xd9, 0xdc, 0xdb,
			0x82, 0x9f, 0xec, 0x33, 0x52, 0xe7, 0xbf, 0x10, 0xb8,
			0x4b, 0xe4, 0xa5,  },
		.test[3].outlen = 16, .test[3].data = { 0x39, 0x6f, 0x99, 0xb5,
			0x43, 0x33, 0x67, 0x4e, 0xd4, 0x45, 0x8f, 0x80, 0x77,
			0xe4, 0xd4, 0x14,  },
		.test[4].outlen = 16, .test[4].data = { 0x73, 0xd4, 0x7c, 0x38,
			0x37, 0x4f, 0x73, 0xd0, 0x78, 0xa8, 0xc6, 0xec, 0x05,
			0x67, 0xca, 0x5e,  },
	},

	/* AESCMAC*/
	{  .min_version = 0x65,
		.test[1].outlen = 16, .test[1].data = { 0x15, 0xbe, 0x1b, 0xfd,
			0x8c, 0xbb, 0xaf, 0x8b, 0x51, 0x9a, 0x64, 0x3b, 0x1b,
			0x46, 0xc1, 0x8f,  },
		.test[3].outlen = 16, .test[3].data = { 0x4e, 0x02, 0xd6, 0xec,
			0x92, 0x75, 0x88, 0xb4, 0x3e, 0x83, 0xa7, 0xac, 0x32,
			0xb6, 0x2b, 0xdb,  },
		.test[4].outlen = 16, .test[4].data = { 0xa7, 0x37, 0x01, 0xbe,
			0xe8, 0xce, 0xed, 0x44, 0x49, 0x4a, 0xbb, 0xf6, 0x9e,
			0xd9, 0x31, 0x3e,  },
	},

	/* KASUMIF9*/
	{  .min_version = 0x65,
		.test[1].outlen = 4, .test[1].data = {  0x5b, 0x26, 0x81, 0x06
		}
	},

	/* SNOW3G UIA2*/
	{  .min_version = 0x65,
		.test[1].outlen = 4, .test[1].data = { 0x08, 0xed, 0x2c, 0x76,
		}
	},

	/* ZUC UIA3*/
	{  .min_version = 0x65,
		.test[1].outlen = 4, .test[1].data = { 0x6a, 0x2b, 0x4c, 0x3a,
		}
	},

	/* POLY1305*/
	{  .min_version = 0x65,
		.test[4].outlen = 16, .test[4].data = { 0xef, 0x91, 0x06, 0x4e,
			0xce, 0x99, 0x9c, 0x4e, 0xfd, 0x05, 0x6a, 0x8c, 0xe6,
			0x18, 0x23, 0xad }
	},

	/* SSLMAC MD5*/
	{  .min_version = 0x65,
		.test[1].outlen = 16, .test[1].data = { 0x0e, 0xf4, 0xca, 0x32,
			0x32, 0x40, 0x1d, 0x1b, 0xaa, 0xfd, 0x6d, 0xa8, 0x01,
			0x79, 0xed, 0xcd,  },
	},

	/* SSLMAC_SHA1*/
	{  .min_version = 0x65,
		.test[2].outlen = 20, .test[2].data = { 0x05, 0x9d, 0x99, 0xb4,
			0xf3, 0x03, 0x1e, 0xc5, 0x24, 0xbf, 0xec, 0xdf, 0x64,
			0x8e, 0x37, 0x2e, 0xf0, 0xef, 0x93, 0xa0,  },
	},

	/* CRC32*/
	{  .min_version = 0x65,
		.test[0].outlen = 0
	},

	/* TKIP-MIC*/
	{  .min_version = 0x65,
		.test[0].outlen = 8, .test[0].data =   { 0x16, 0xfb, 0xa0,
			0x0e, 0xe2, 0xab, 0x6c, 0x97,  }
	},

	/* SHA3-224*/
	{  .min_version = 0x65,
		.test[1].outlen = 28, .test[1].data =  { 0x73, 0xe0, 0x87,
			0xae, 0x12, 0x71, 0xb2, 0xc5, 0xf6, 0x85, 0x46, 0xc9,
			0x3a, 0xb4, 0x25, 0x14, 0xa6, 0x9e, 0xef, 0x25, 0x2b,
			0xfd, 0xd1, 0x37, 0x55, 0x74, 0x8a, 0x00,  }
	},

	/* SHA3-256*/
	{  .min_version = 0x65,
		.test[1].outlen = 32, .test[1].data = { 0x9e, 0x62, 0x91, 0x97,
			0x0c, 0xb4, 0x4d, 0xd9, 0x40, 0x08, 0xc7, 0x9b, 0xca,
			0xf9, 0xd8, 0x6f, 0x18, 0xb4, 0xb4, 0x9b, 0xa5, 0xb2,
			0xa0, 0x47, 0x81, 0xdb, 0x71, 0x99, 0xed, 0x3b, 0x9e,
			0x4e,  }
	},

	/* SHA3-384*/
	{  .min_version = 0x65,
		.test[1].outlen = 48, .test[1].data =  { 0x4b, 0xda, 0xab,
			0xf7, 0x88, 0xd3, 0xad, 0x1a, 0xd8, 0x3d, 0x6d, 0x93,
			0xc7, 0xe4, 0x49, 0x37, 0xc2, 0xe6, 0x49, 0x6a, 0xf2,
			0x3b, 0xe3, 0x35, 0x4d, 0x75, 0x69, 0x87, 0xf4, 0x51,
			0x60, 0xfc, 0x40, 0x23, 0xbd, 0xa9, 0x5e, 0xcd, 0xcb,
			0x3c, 0x7e, 0x31, 0xa6, 0x2f, 0x72, 0x6d, 0x70, 0x2c,
		}
	},

	/* SHA3-512*/
	{  .min_version = 0x65,
		.test[1].outlen = 64, .test[1].data = { 0xad, 0x56, 0xc3, 0x5c,
			0xab, 0x50, 0x63, 0xb9, 0xe7, 0xea, 0x56, 0x83, 0x14,
			0xec, 0x81, 0xc4, 0x0b, 0xa5, 0x77, 0xaa, 0xe6, 0x30,
			0xde, 0x90, 0x20, 0x04, 0x00, 0x9e, 0x88, 0xf1, 0x8d,
			0xa5, 0x7b, 0xbd, 0xfd, 0xaa, 0xa0, 0xfc, 0x18, 0x9c,
			0x66, 0xc8, 0xd8, 0x53, 0x24, 0x8b, 0x6b, 0x11, 0x88,
			0x44, 0xd5, 0x3f, 0x7d, 0x0b, 0xa1, 0x1d, 0xe0, 0xf3,
			0xbf, 0xaf, 0x4c, 0xdd, 0x9b, 0x3f,  }
	},

	/* SHAKE128*/
	{  .min_version = 0x65,
		.test[4].outlen = 16, .test[4].data =  { 0x24, 0xa7, 0xca,
			0x4b, 0x75, 0xe3, 0x89, 0x8d, 0x4f, 0x12, 0xe7, 0x4d,
			0xea, 0x8c, 0xbb, 0x65 }
	},

	/* SHAKE256*/
	{  .min_version = 0x65,
		.test[4].outlen = 32, .test[4].data =  { 0xf5, 0x97, 0x7c,
			0x82, 0x83, 0x54, 0x6a, 0x63, 0x72, 0x3b, 0xc3, 0x1d,
			0x26, 0x19, 0x12, 0x4f,
			0x11, 0xdb, 0x46, 0x58, 0x64, 0x33, 0x36, 0x74, 0x1d,
			0xf8, 0x17, 0x57, 0xd5, 0xad, 0x30, 0x62 }
	},

	/* CSHAKE128*/
	{  .min_version = 0x65,
		.test[1].outlen = 16, .test[1].data =  { 0xe0, 0x6f, 0xd8,
			0x50, 0x57, 0x6f, 0xe4, 0xfa, 0x7e, 0x13, 0x42, 0xb5,
			0xf8, 0x13, 0xeb, 0x23 }
	},

	/* CSHAKE256*/
	{  .min_version = 0x65,
		.test[1].outlen = 32, .test[1].data =  { 0xf3, 0xf2, 0xb5,
			0x47, 0xf2, 0x16, 0xba, 0x6f, 0x49, 0x83, 0x3e, 0xad,
			0x1e, 0x46, 0x85, 0x54,
			0xd0, 0xd7, 0xf9, 0xc6, 0x7e, 0xe9, 0x27, 0xc6, 0xc3,
			0xc3, 0xdb, 0x91, 0xdb, 0x97, 0x04, 0x0f }
	},

	/* KMAC128*/
	{  .min_version = 0x65,
		.test[1].outlen = 16, .test[1].data =  { 0x6c, 0x3f, 0x29,
			0xfe, 0x01, 0x96, 0x59, 0x36, 0xb7, 0xae, 0xb7, 0xff,
			0x71, 0xe0, 0x3d, 0xff },
		.test[4].outlen = 16, .test[4].data =  { 0x58, 0xd9, 0x8d,
			0xe8, 0x1f, 0x64, 0xb4, 0xa3, 0x9f, 0x63, 0xaf, 0x21,
			0x99, 0x03, 0x97, 0x06 },
		.test[5].outlen = 16, .test[5].data =  { 0xf8, 0xf9, 0xb7,
			0xa4, 0x05, 0x3d, 0x90, 0x7c, 0xf2, 0xa1, 0x7c, 0x34,
			0x39, 0xc2, 0x87, 0x4b },
		.test[6].outlen = 16, .test[6].data =  { 0xef, 0x4a, 0xd5,
			0x1d, 0xd7, 0x83, 0x56, 0xd3, 0xa8, 0x3c, 0xf5, 0xf8,
			0xd1, 0x12, 0xf4, 0x44 }
	},

	/* KMAC256*/
	{  .min_version = 0x65,
		.test[1].outlen = 32, .test[1].data =  { 0x0d, 0x86, 0xfa,
			0x92, 0x92, 0xe4, 0x77, 0x24, 0x6a, 0xcc, 0x79, 0xa0,
			0x1e, 0xb4, 0xc3, 0xac,
			0xfc, 0x56, 0xbc, 0x63, 0xcc, 0x1b, 0x6e, 0xf6, 0xc8,
			0x99, 0xa5, 0x3a, 0x38, 0x14, 0xa2, 0x40 },
		.test[4].outlen = 32, .test[4].data =  { 0xad, 0x99, 0xed,
			0x20, 0x1f, 0xbe, 0x45, 0x07, 0x3d, 0xf4, 0xae, 0x9f,
			0xc2, 0xd8, 0x06, 0x18,
			0x31, 0x4e, 0x8c, 0xb6, 0x33, 0xe8, 0x31, 0x36, 0x00,
			0xdd, 0x42, 0x20, 0xda, 0x2b, 0xd5, 0x2b },
		.test[5].outlen = 32, .test[5].data =  { 0xf9, 0xc6, 0x2b,
			0x17, 0xa0, 0x04, 0xd9, 0xf2, 0x6c, 0xbf, 0x5d, 0xa5,
			0x9a, 0xd7, 0x36, 0x1d,
			0xad, 0x66, 0x6b, 0x3d, 0xb1, 0x52, 0xd3, 0x81, 0x39,
			0x20, 0xd4, 0xf0, 0x43, 0x72, 0x2c, 0xb7 },
		.test[6].outlen = 32, .test[6].data =  { 0xcc, 0x89, 0xe4,
			0x05, 0x58, 0x77, 0x38, 0x8b, 0x18, 0xa0, 0x7c, 0x8d,
			0x20, 0x99, 0xea, 0x6e,
			0x6b, 0xe9, 0xf7, 0x0c, 0xe1, 0xe5, 0xce, 0xbc, 0x55,
			0x4c, 0x80, 0xa5, 0xdc, 0xae, 0xf7, 0x94 }
	},

	/* KMAC128XOF*/
	{  .min_version = 0x65,
		.test[1].outlen = 16, .test[1].data =  { 0x84, 0x07, 0x89,
			0x29, 0xa7, 0xf4, 0x98, 0x91, 0xf5, 0x64, 0x61, 0x8d,
			0xa5, 0x93, 0x00, 0x31 },
		.test[4].outlen = 16, .test[4].data =  { 0xf0, 0xa4, 0x1b,
			0x98, 0x0f, 0xb3, 0xf2, 0xbd, 0xc3, 0xfc, 0x64, 0x1f,
			0x73, 0x1f, 0xd4, 0x74 },
		.test[5].outlen = 16, .test[5].data =  { 0xa5, 0xc5, 0xad,
			0x25, 0x59, 0xf1, 0x5d, 0xea, 0x5b, 0x18, 0x0a, 0x52,
			0xce, 0x6c, 0xc0, 0x88 },
		.test[6].outlen = 16, .test[6].data =  { 0x1a, 0x81, 0xdd,
			0x81, 0x47, 0x89, 0xf4, 0x15, 0xcc, 0x18, 0x05, 0x81,
			0xe3, 0x95, 0x21, 0xc3 }
	},

	/* KMAC256XOF*/
	{  .min_version = 0x65,
		.test[1].outlen = 32, .test[1].data =  { 0xff, 0x85, 0xe9,
			0x61, 0x67, 0x96, 0x35, 0x58, 0x33, 0x38, 0x2c, 0xe8,
			0x25, 0x77, 0xbe, 0x63,
			0xd5, 0x2c, 0xa7, 0xef, 0xce, 0x9b, 0x63, 0x71, 0xb2,
			0x09, 0x7c, 0xd8, 0x60, 0x4e, 0x5a, 0xfa },
		.test[4].outlen = 32, .test[4].data =  { 0x86, 0x89, 0xc2,
			0x4a, 0xe8, 0x18, 0x46, 0x10, 0x6b, 0xf2, 0x09, 0xd7,
			0x37, 0x83, 0xab, 0x77,
			0xb5, 0xce, 0x7c, 0x96, 0x9c, 0xfa, 0x0f, 0xa0, 0xd8,
			0xde, 0xb5, 0xb7, 0xc6, 0xcd, 0xa9, 0x8f },
		.test[5].outlen = 32, .test[5].data =  { 0x4d, 0x71, 0x81,
			0x5a, 0x5f, 0xac, 0x3b, 0x29, 0xf2, 0x5f, 0xb6, 0x56,
			0xf1, 0x76, 0xcf, 0xdc,
			0x51, 0x56, 0xd7, 0x3c, 0x47, 0xec, 0x6d, 0xea, 0xc6,
			0x3e, 0x54, 0xe7, 0x6f, 0xdc, 0xe8, 0x39 },
		.test[6].outlen = 32, .test[6].data =  { 0x5f, 0xc5, 0xe1,
			0x1e, 0xe7, 0x55, 0x0f, 0x62, 0x71, 0x29, 0xf3, 0x0a,
			0xb3, 0x30, 0x68, 0x06,
			0xea, 0xec, 0xe4, 0x37, 0x17, 0x37, 0x2d, 0x5d, 0x64,
			0x09, 0x70, 0x63, 0x94, 0x80, 0x9b, 0x80 }
	},

	/* HASH SM3*/
	{  .min_version = 0x65,
		.test[1].outlen = 32, .test[1].data =  { 0xe0, 0xba, 0xb8,
			0xf4, 0xd8, 0x17, 0x2b, 0xa2, 0x45, 0x19, 0x0d, 0x13,
			0xc9, 0x41, 0x17, 0xe9,
			0x3b, 0x82, 0x16, 0x6c, 0x25, 0xb2, 0xb6, 0x98, 0x83,
			0x35, 0x0c, 0x19, 0x2c, 0x90, 0x51, 0x40 },
		.test[4].outlen = 32, .test[4].data =  { 0xe0, 0xba, 0xb8,
			0xf4, 0xd8, 0x17, 0x2b, 0xa2, 0x45, 0x19, 0x0d, 0x13,
			0xc9, 0x41, 0x17, 0xe9,
			0x3b, 0x82, 0x16, 0x6c, 0x25, 0xb2, 0xb6, 0x98, 0x83,
			0x35, 0x0c, 0x19, 0x2c, 0x90, 0x51, 0x40 },
		.test[5].outlen = 32, .test[5].data =  { 0xe0, 0xba, 0xb8,
			0xf4, 0xd8, 0x17, 0x2b, 0xa2, 0x45, 0x19, 0x0d, 0x13,
			0xc9, 0x41, 0x17, 0xe9,
			0x3b, 0x82, 0x16, 0x6c, 0x25, 0xb2, 0xb6, 0x98, 0x83,
			0x35, 0x0c, 0x19, 0x2c, 0x90, 0x51, 0x40 },
		.test[6].outlen = 32, .test[6].data =  { 0xe0, 0xba, 0xb8,
			0xf4, 0xd8, 0x17, 0x2b, 0xa2, 0x45, 0x19, 0x0d, 0x13,
			0xc9, 0x41, 0x17, 0xe9,
			0x3b, 0x82, 0x16, 0x6c, 0x25, 0xb2, 0xb6, 0x98, 0x83,
			0x35, 0x0c, 0x19, 0x2c, 0x90, 0x51, 0x40 }
	},

	/* HMAC SM3*/
	{  .min_version = 0x65,
		.test[1].outlen = 32, .test[1].data =  { 0x68, 0xf0, 0x65,
			0xd8, 0xd8, 0xc9, 0xc2, 0x0e, 0x10, 0xfd, 0x52, 0x7c,
			0xf2, 0xd7, 0x42, 0xd3,
			0x08, 0x44, 0x22, 0xbc, 0xf0, 0x9d, 0xcc, 0x34, 0x7b,
			0x76, 0x13, 0x91, 0xba, 0xce, 0x4d, 0x17 },
		.test[4].outlen = 32, .test[4].data =  { 0xd8, 0xab, 0x2a,
			0x7b, 0x56, 0x21, 0xb1, 0x59, 0x64, 0xb2, 0xa3, 0xd6,
			0x72, 0xb3, 0x95, 0x81,
			0xa0, 0xcd, 0x96, 0x47, 0xf0, 0xbc, 0x8c, 0x16, 0x5b,
			0x9b, 0x7d, 0x2f, 0x71, 0x3f, 0x23, 0x19},
		.test[5].outlen = 32, .test[5].data =  { 0xa0, 0xd1, 0xd5,
			0xa0, 0x9e, 0x4c, 0xca, 0x8c, 0x7b, 0xe0, 0x8f, 0x70,
			0x92, 0x2e, 0x3f, 0x4c,
			0xa0, 0xca, 0xef, 0xa1, 0x86, 0x9d, 0xb2, 0xe1, 0xc5,
			0xfa, 0x9d, 0xfa, 0xbc, 0x11, 0xcb, 0x1f },
		.test[6].outlen = 32, .test[6].data =  { 0xa0, 0xd1, 0xd5,
			0xa0, 0x9e, 0x4c, 0xca, 0x8c, 0x7b, 0xe0, 0x8f, 0x70,
			0x92, 0x2e, 0x3f, 0x4c,
			0xa0, 0xca, 0xef, 0xa1, 0x86, 0x9d, 0xb2, 0xe1, 0xc5,
			0xfa, 0x9d, 0xfa, 0xbc, 0x11, 0xcb, 0x1f}
	},

	/* MAC_SM4_XCBC*/
	{  .min_version = 0x65,
		.test[1].outlen = 16, .test[1].data =  { 0x69, 0xaf, 0x45,
			0xe6, 0x0c, 0x78, 0x71, 0x7e, 0x44, 0x6c, 0xfe, 0x68,
			0xd4, 0xfe, 0x20, 0x8b },
		.test[4].outlen = 16, .test[4].data =  { 0x69, 0xaf, 0x45,
			0xe6, 0x0c, 0x78, 0x71, 0x7e, 0x44, 0x6c, 0xfe, 0x68,
			0xd4, 0xfe, 0x20, 0x8b },
		.test[5].outlen = 16, .test[5].data =  { 0x69, 0xaf, 0x45,
			0xe6, 0x0c, 0x78, 0x71, 0x7e, 0x44, 0x6c, 0xfe, 0x68,
			0xd4, 0xfe, 0x20, 0x8b },
		.test[6].outlen = 16, .test[6].data =  { 0x69, 0xaf, 0x45,
			0xe6, 0x0c, 0x78, 0x71, 0x7e, 0x44, 0x6c, 0xfe, 0x68,
			0xd4, 0xfe, 0x20, 0x8b }
	},

	/* MAC_SM4_CMAC*/
	{  .min_version = 0x65,
		.test[1].outlen = 16, .test[1].data =  { 0x36, 0xbe, 0xec,
			0x03, 0x9c, 0xc7, 0x0c, 0x28, 0x23, 0xdd, 0x71, 0x8b,
			0x3c, 0xbd, 0x7f, 0x37 },
		.test[4].outlen = 16, .test[4].data =  { 0x36, 0xbe, 0xec,
			0x03, 0x9c, 0xc7, 0x0c, 0x28, 0x23, 0xdd, 0x71, 0x8b,
			0x3c, 0xbd, 0x7f, 0x37 },
		.test[5].outlen = 16, .test[5].data =  { 0x36, 0xbe, 0xec,
			0x03, 0x9c, 0xc7, 0x0c, 0x28, 0x23, 0xdd, 0x71, 0x8b,
			0x3c, 0xbd, 0x7f, 0x37 },
		.test[6].outlen = 16, .test[6].data =  { 0x36, 0xbe, 0xec,
			0x03, 0x9c, 0xc7, 0x0c, 0x28, 0x23, 0xdd, 0x71, 0x8b,
			0x3c, 0xbd, 0x7f, 0x37 }
	},

};
#endif

int spacc_sg_to_ddt(struct device *dev, struct scatterlist *sg,
		    int nbytes, struct pdu_ddt *ddt, int dma_direction)
{
	struct scatterlist *sg_entry, *sgl;
	int nents, orig_nents;
	int i, rc;

	orig_nents = sg_nents(sg);
	if (orig_nents > 1) {
		sgl = sg_last(sg, orig_nents);
		if (sgl->length == 0)
			orig_nents--;
	}
	nents = dma_map_sg(dev, sg, orig_nents, dma_direction);

	if (nents <= 0)
		return -ENOMEM;

	/* require ATOMIC operations */
	rc = pdu_ddt_init(ddt, nents | 0x80000000);
	if (rc < 0) {
		dma_unmap_sg(dev, sg, nents, dma_direction);
		return -EIO;
	}

	for_each_sg(sg, sg_entry, nents, i) {
		pdu_ddt_add(ddt, sg_dma_address(sg_entry),
			    sg_dma_len(sg_entry));
	}

	dma_sync_sg_for_device(dev, sg, nents, dma_direction);

	return nents;
}

int spacc_set_operation(struct spacc_device *spacc, int handle, int op,
			u32 prot, uint32_t icvcmd, uint32_t icvoff,
			uint32_t icvsz, uint32_t sec_key)
{
	int ret = CRYPTO_OK;
	struct spacc_job *job = NULL;

	if (handle < 0 || handle > SPACC_MAX_JOBS)
		return -ENXIO;

	job = &spacc->job[handle];
	if (!job)
		return -EIO;

	job->op = op;
	if (op == OP_ENCRYPT)
		job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_ENCRYPT);
	else
		job->ctrl &= ~SPACC_CTRL_MASK(SPACC_CTRL_ENCRYPT);

	switch (prot) {
	case ICV_HASH:        /* HASH of plaintext */
		job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_ICV_PT);
		break;
	case ICV_HASH_ENCRYPT:
		/* HASH the plaintext and encrypt the lot */
		/* ICV_PT and ICV_APPEND must be set too */
		job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_ICV_ENC);
		job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_ICV_PT);
		 /* This mode is not valid when BIT_ALIGN != 0 */
		job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_ICV_APPEND);
		break;
	case ICV_ENCRYPT_HASH: /* HASH the ciphertext */
		job->ctrl &= ~SPACC_CTRL_MASK(SPACC_CTRL_ICV_PT);
		job->ctrl &= ~SPACC_CTRL_MASK(SPACC_CTRL_ICV_ENC);
		break;
	case ICV_IGNORE:
		break;
	default:
		ret = -EINVAL;
	}

	job->icv_len = icvsz;

	switch (icvcmd) {
	case IP_ICV_OFFSET:
		job->icv_offset = icvoff;
		job->ctrl &= ~SPACC_CTRL_MASK(SPACC_CTRL_ICV_APPEND);
		break;
	case IP_ICV_APPEND:
		job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_ICV_APPEND);
		break;
	case IP_ICV_IGNORE:
		break;
	default:
		ret = -EINVAL;
	}

	if (sec_key)
		job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_SEC_KEY);

	return ret;
}

static int _spacc_fifo_full(struct spacc_device *spacc, uint32_t prio)
{
	if (spacc->config.is_qos)
		return readl(spacc->regmap + SPACC_REG_FIFO_STAT) &
		       SPACC_FIFO_STAT_CMDX_FULL(prio);
	else
		return readl(spacc->regmap + SPACC_REG_FIFO_STAT) &
		       SPACC_FIFO_STAT_CMD0_FULL;
}

/* When proc_sz != 0 it overrides the ddt_len value
 * defined in the context referenced by 'job_idx'
 */
int spacc_packet_enqueue_ddt_ex(struct spacc_device *spacc, int use_jb,
				int job_idx, struct pdu_ddt *src_ddt,
				struct pdu_ddt *dst_ddt, u32 proc_sz,
				uint32_t aad_offset, uint32_t pre_aad_sz,
				u32 post_aad_sz, uint32_t iv_offset,
				uint32_t prio)
{
	int i;
	struct spacc_job *job;
	int ret = CRYPTO_OK, proc_len;

	if (job_idx < 0 || job_idx > SPACC_MAX_JOBS)
		return -ENXIO;

	switch (prio)  {
	case SPACC_SW_CTRL_PRIO_MED:
		if (spacc->config.cmd1_fifo_depth == 0)
			return -EINVAL;
		break;
	case SPACC_SW_CTRL_PRIO_LOW:
		if (spacc->config.cmd2_fifo_depth == 0)
			return -EINVAL;
		break;
	}

	job = &spacc->job[job_idx];
	if (!job)
		return -EIO;

	/* process any jobs in the jb*/
	if (use_jb && spacc_process_jb(spacc) != 0)
		goto fifo_full;

	if (_spacc_fifo_full(spacc, prio)) {
		if (use_jb)
			goto fifo_full;
		else
			return -EBUSY;
	}

	/* compute the length we must process, in decrypt mode
	 * with an ICV (hash, hmac or CCM modes)
	 * we must subtract the icv length from the buffer size
	 */
	if (proc_sz == SPACC_AUTO_SIZE) {
		proc_len = src_ddt->len;

		if (job->op == OP_DECRYPT &&
		    (job->hash_mode > 0 ||
		     job->enc_mode == CRYPTO_MODE_AES_CCM ||
		     job->enc_mode == CRYPTO_MODE_AES_GCM)  &&
		    !(job->ctrl & SPACC_CTRL_MASK(SPACC_CTRL_ICV_ENC)))
			proc_len = src_ddt->len - job->icv_len;
	} else {
		proc_len = proc_sz;
	}

	if (pre_aad_sz & SPACC_AADCOPY_FLAG) {
		job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_AAD_COPY);
		pre_aad_sz &= ~(SPACC_AADCOPY_FLAG);
	} else {
		job->ctrl &= ~SPACC_CTRL_MASK(SPACC_CTRL_AAD_COPY);
	}

	job->pre_aad_sz  = pre_aad_sz;
	job->post_aad_sz = post_aad_sz;

	if (spacc->config.dma_type == SPACC_DMA_DDT) {
		pdu_io_cached_write(spacc->regmap + SPACC_REG_SRC_PTR,
				    (uint32_t)src_ddt->phys,
				    &spacc->cache.src_ptr);
		pdu_io_cached_write(spacc->regmap + SPACC_REG_DST_PTR,
				    (uint32_t)dst_ddt->phys,
				    &spacc->cache.dst_ptr);
	} else if (spacc->config.dma_type == SPACC_DMA_LINEAR) {
		pdu_io_cached_write(spacc->regmap + SPACC_REG_SRC_PTR,
				    (uint32_t)src_ddt->virt[0],
				    &spacc->cache.src_ptr);
		pdu_io_cached_write(spacc->regmap + SPACC_REG_DST_PTR,
				    (uint32_t)dst_ddt->virt[0],
				    &spacc->cache.dst_ptr);
	} else {
		return -EIO;
	}

	pdu_io_cached_write(spacc->regmap + SPACC_REG_PROC_LEN,
			    proc_len - job->post_aad_sz,
			    &spacc->cache.proc_len);
	pdu_io_cached_write(spacc->regmap + SPACC_REG_ICV_LEN,
			    job->icv_len, &spacc->cache.icv_len);
	pdu_io_cached_write(spacc->regmap + SPACC_REG_ICV_OFFSET,
			    job->icv_offset, &spacc->cache.icv_offset);
	pdu_io_cached_write(spacc->regmap + SPACC_REG_PRE_AAD_LEN,
			    job->pre_aad_sz, &spacc->cache.pre_aad);
	pdu_io_cached_write(spacc->regmap + SPACC_REG_POST_AAD_LEN,
			    job->post_aad_sz, &spacc->cache.post_aad);
	pdu_io_cached_write(spacc->regmap + SPACC_REG_IV_OFFSET,
			    iv_offset, &spacc->cache.iv_offset);
	pdu_io_cached_write(spacc->regmap + SPACC_REG_OFFSET,
			    aad_offset, &spacc->cache.offset);
	pdu_io_cached_write(spacc->regmap + SPACC_REG_AUX_INFO,
			    AUX_DIR(job->auxinfo_dir) |
			    AUX_BIT_ALIGN(job->auxinfo_bit_align) |
			    AUX_CBC_CS(job->auxinfo_cs_mode),
			    &spacc->cache.aux);

	if (job->first_use == 1) {
		writel(job->ckey_sz | SPACC_SET_KEY_CTX(job->ctx_idx),
		       spacc->regmap + SPACC_REG_KEY_SZ);
		writel(job->hkey_sz | SPACC_SET_KEY_CTX(job->ctx_idx),
		       spacc->regmap + SPACC_REG_KEY_SZ);
	}

	job->job_swid = spacc->job_next_swid;
	spacc->job_lookup[job->job_swid] = job_idx;
	spacc->job_next_swid =
		(spacc->job_next_swid + 1) % SPACC_MAX_JOBS;
	writel(SPACC_SW_CTRL_ID_SET(job->job_swid) |
	       SPACC_SW_CTRL_PRIO_SET(prio),
	       spacc->regmap + SPACC_REG_SW_CTRL);
	writel(job->ctrl, spacc->regmap + SPACC_REG_CTRL);

	/* Clear an expansion key after the first call*/
	if (job->first_use == 1) {
		job->first_use = 0;
		job->ctrl &= ~SPACC_CTRL_MASK(SPACC_CTRL_KEY_EXP);
	}

	return ret;

fifo_full:
	/* try to add a job to the job buffers*/
	i = spacc->jb_head + 1;
	if (i == SPACC_MAX_JOB_BUFFERS)
		i = 0;

	if (i == spacc->jb_tail)
		return -EBUSY;

	spacc->job_buffer[spacc->jb_head] = (struct spacc_job_buffer) {
		.active		= 1,
		.job_idx	= job_idx,
		.src		= src_ddt,
		.dst		= dst_ddt,
		.proc_sz	= proc_sz,
		.aad_offset	= aad_offset,
		.pre_aad_sz	= pre_aad_sz,
		.post_aad_sz	= post_aad_sz,
		.iv_offset	= iv_offset,
		.prio		= prio
	};

	spacc->jb_head = i;

	return CRYPTO_USED_JB;
}

int spacc_packet_enqueue_ddt(struct spacc_device *spacc, int job_idx,
			     struct pdu_ddt *src_ddt, struct pdu_ddt *dst_ddt,
			     u32 proc_sz, u32 aad_offset, uint32_t pre_aad_sz,
			     uint32_t post_aad_sz, u32 iv_offset, uint32_t prio)
{
	int ret;
	unsigned long lock_flags;

	spin_lock_irqsave(&spacc->lock, lock_flags);
	ret = spacc_packet_enqueue_ddt_ex(spacc, 1, job_idx, src_ddt,
					  dst_ddt, proc_sz, aad_offset,
					  pre_aad_sz, post_aad_sz,
					  iv_offset, prio);
	spin_unlock_irqrestore(&spacc->lock, lock_flags);

	return ret;
}

static int spacc_packet_dequeue(struct spacc_device *spacc, int job_idx)
{
	int ret = CRYPTO_OK;
	struct spacc_job *job = &spacc->job[job_idx];
	unsigned long lock_flag;

	spin_lock_irqsave(&spacc->lock, lock_flag);

	if (!job && !(job_idx == SPACC_JOB_IDX_UNUSED)) {
		ret = -EIO;
	} else if (job->job_done) {
		job->job_done  = 0;
		ret = job->job_err;
	} else {
		ret = -EINPROGRESS;
	}

	spin_unlock_irqrestore(&spacc->lock, lock_flag);

	return ret;
}

int spacc_isenabled(struct spacc_device *spacc, int mode, int keysize)
{
	int x;

	if (mode < 0 || mode > CRYPTO_MODE_LAST)
		return 0;

	if (mode == CRYPTO_MODE_NULL    ||
	    mode == CRYPTO_MODE_AES_XTS ||
	    mode == CRYPTO_MODE_SM4_XTS ||
	    mode == CRYPTO_MODE_AES_F8  ||
	    mode == CRYPTO_MODE_SM4_F8  ||
	    spacc->config.modes[mode] & 128)
		return 1;

	for (x = 0; x < 6; x++) {
		if (keysizes[0][x] == keysize) {
			if (spacc->config.modes[mode] & (1 << x))
				return 1;
			else
				return 0;
		}
	}

	return 0;
}

/* Releases a crypto context back into appropriate module's pool*/
int spacc_close(struct spacc_device *dev, int handle)
{
	return spacc_job_release(dev, handle);
}

#if IS_ENABLED(CONFIG_CRYPTO_DEV_SPACC_AUTODETECT)
static int spacc_set_auxinfo(struct spacc_device *spacc, int jobid,
			     uint32_t direction, uint32_t bitsize)
{
	int ret = CRYPTO_OK;
	struct spacc_job *job;

	if (jobid < 0 || jobid > SPACC_MAX_JOBS)
		return -ENXIO;

	job = &spacc->job[jobid];
	if (!job) {
		ret = -EIO;
	} else {
		job->auxinfo_dir = direction;
		job->auxinfo_bit_align = bitsize;
	}

	return ret;
}

static void check_modes(struct spacc_device *spacc, int x, int y, void *virt,
			char *key, struct pdu_ddt *ddt)
{
	int proclen, aadlen, ivsize, h, err, enc, hash;

	if (template[x] & (1 << y)) {
		/* testing keysizes[y] with algo 'x' which
		 * should match the ENUMs above
		 */

		if (template[x] & 128) {
			enc = 0;
			hash = x;
		} else {
			enc = x;
			hash = 0;
		}

		h = spacc_open(spacc, enc, hash, -1, 0, NULL, NULL);
		if (h < 0) {
			spacc->config.modes[x] &= ~(1 << y);
			return;
		}

		spacc_set_operation(spacc, h, OP_ENCRYPT, 0, IP_ICV_APPEND, 0,
				    0, 0);

		/* if this is a hash or mac*/
		if (template[x] & 128) {
			switch (x) {
			case CRYPTO_MODE_HASH_CSHAKE128:
			case CRYPTO_MODE_HASH_CSHAKE256:
			case CRYPTO_MODE_MAC_KMAC128:
			case CRYPTO_MODE_MAC_KMAC256:
			case CRYPTO_MODE_MAC_KMACXOF128:
			case CRYPTO_MODE_MAC_KMACXOF256:
				/* special initial bytes to encode
				 * length for cust strings
				 */
				key[0] = 0x01;
				key[1] = 0x70;
				break;
			}

			spacc_write_context(spacc, h, SPACC_HASH_OPERATION,
					    key, keysizes[1][y] +
					(x == CRYPTO_MODE_MAC_XCBC ? 32 : 0),
					key, 16);
		} else {
			u32 keysize;

			ivsize = 16;
			keysize = keysizes[0][y];
			switch (x) {
			case CRYPTO_MODE_CHACHA20_STREAM:
			case CRYPTO_MODE_AES_CCM:
			case CRYPTO_MODE_SM4_CCM:
				ivsize = 16;
				break;
			case CRYPTO_MODE_SM4_GCM:
			case CRYPTO_MODE_CHACHA20_POLY1305:
			case CRYPTO_MODE_AES_GCM:
				ivsize = 12;
				break;
			case CRYPTO_MODE_KASUMI_ECB:
			case CRYPTO_MODE_KASUMI_F8:
			case CRYPTO_MODE_3DES_CBC:
			case CRYPTO_MODE_3DES_ECB:
			case CRYPTO_MODE_DES_CBC:
			case CRYPTO_MODE_DES_ECB:
				ivsize = 8;
				break;
			case CRYPTO_MODE_SM4_XTS:
			case CRYPTO_MODE_AES_XTS:
				keysize <<= 1;
				break;
			}
			spacc_write_context(spacc, h, SPACC_CRYPTO_OPERATION,
					    key, keysize, key, ivsize);
		}

		spacc_set_key_exp(spacc, h);

		switch (x) {
		case CRYPTO_MODE_ZUC_UEA3:
		case CRYPTO_MODE_SNOW3G_UEA2:
		case CRYPTO_MODE_MAC_SNOW3G_UIA2:
		case CRYPTO_MODE_MAC_ZUC_UIA3:
		case CRYPTO_MODE_KASUMI_F8:
			spacc_set_auxinfo(spacc, h, 0, 0);
			break;
		case CRYPTO_MODE_MAC_KASUMI_F9:
			spacc_set_auxinfo(spacc, h, 0, 8);
			break;
		}

		memset(virt, 0, 256);

		/* 16AAD/16PT or 32AAD/0PT depending on
		 * whether we're in a hash or not mode
		 */
		aadlen = 16;
		proclen = 32;
		if (!enc)
			aadlen += 16;

		switch (x) {
		case CRYPTO_MODE_SM4_CS1:
		case CRYPTO_MODE_SM4_CS2:
		case CRYPTO_MODE_SM4_CS3:
		case CRYPTO_MODE_AES_CS1:
		case CRYPTO_MODE_AES_CS2:
		case CRYPTO_MODE_AES_CS3:
			proclen = 31;
			fallthrough;
		case CRYPTO_MODE_SM4_XTS:
		case CRYPTO_MODE_AES_XTS:
			aadlen = 0;
		}

		err = spacc_packet_enqueue_ddt(spacc, h, ddt, ddt, proclen, 0,
					       aadlen, 0, 0, 0);
		if (err == CRYPTO_OK) {
			do {
				err = spacc_packet_dequeue(spacc, h);
			} while (err == -EINPROGRESS);
		}
		if (err != CRYPTO_OK || !testdata[x].test[y].outlen ||
			memcmp(testdata[x].test[y].data, virt,
			       testdata[x].test[y].outlen)) {
			spacc->config.modes[x] &= ~(1 << y);
		}
		spacc_close(spacc, h);
	}
}

int spacc_autodetect(struct spacc_device *spacc)
{
	struct pdu_ddt ddt;
	dma_addr_t dma;
	void *virt;
	int x, y;
	unsigned char key[64];

	/* allocate DMA memory ...*/
	virt = dma_alloc_coherent(get_ddt_device(), 256, &dma, GFP_KERNEL);
	if (!virt)
		return -2;

	if (pdu_ddt_init(&ddt, 1)) {
		dma_free_coherent(get_ddt_device(), 256, virt, dma);
		return -3;
	}

	pdu_ddt_add(&ddt, dma, 256);

	for (x = 0; x < 64; x++)
		key[x] = x;

	for (x = 0; x < ARRAY_SIZE(template); x++) {
		spacc->config.modes[x] = template[x];
		if (template[x] && spacc->config.version >=
				testdata[x].min_version) {
			for (y = 0; y < (ARRAY_SIZE(keysizes[0])); y++)
				check_modes(spacc, x, y, virt, key, &ddt);
		}
	}

	pdu_ddt_free(&ddt);
	dma_free_coherent(get_ddt_device(), 256, virt, dma);

	return 0;
}

#else

static void spacc_static_modes(struct spacc_device *spacc, int x, int y)
{
	/* Disable the algos that as not supported here */
	switch (x) {
	case CRYPTO_MODE_AES_F8:
	case CRYPTO_MODE_AES_CFB:
	case CRYPTO_MODE_AES_OFB:
	case CRYPTO_MODE_MULTI2_ECB:
	case CRYPTO_MODE_MULTI2_CBC:
	case CRYPTO_MODE_MULTI2_CFB:
	case CRYPTO_MODE_MULTI2_OFB:
	case CRYPTO_MODE_MAC_POLY1305:
	case CRYPTO_MODE_HASH_CRC32:
		/* Disable the modes */
		spacc->config.modes[x] &= ~(1 << y);
		break;
	default:
		break;/* Algos are enabled */
	}
}

int spacc_static_config(struct spacc_device *spacc)
{

	int x, y;

	for (x = 0; x < ARRAY_SIZE(template); x++) {
		spacc->config.modes[x] = template[x];

		for (y = 0; y < (ARRAY_SIZE(keysizes[0])); y++) {
			/* List static modes */
			spacc_static_modes(spacc, x, y);
		}
	}

	return 0;
}
#endif
int spacc_clone_handle(struct spacc_device *spacc, int old_handle,
		       void *cbdata)
{
	int new_handle;

	new_handle = spacc_job_request(spacc, spacc->job[old_handle].ctx_idx);
	if (new_handle < 0)
		return new_handle;

	spacc->job[new_handle]          = spacc->job[old_handle];
	spacc->job[new_handle].job_used = new_handle;
	spacc->job[new_handle].cbdata   = cbdata;

	return new_handle;
}

/* Allocates a job for spacc module context and initialize
 * it with an appropriate type.
 */
int spacc_open(struct spacc_device *spacc, int enc, int hash, int ctxid,
	       int secure_mode, spacc_callback cb, void *cbdata)
{
	u32 ctrl = 0;
	int job_idx = 0;
	int ret = CRYPTO_OK;
	struct spacc_job *job = NULL;

	job_idx = spacc_job_request(spacc, ctxid);
	if (job_idx < 0)
		return -EIO;

	job = &spacc->job[job_idx];

	if (secure_mode && job->ctx_idx > spacc->config.num_sec_ctx) {
		pr_debug("ERR: For secure contexts");
		pr_debug("ERR: Job ctx ID is outside allowed range\n");
		spacc_job_release(spacc, job_idx);
		return -EIO;
	}

	job->auxinfo_cs_mode	= 0;
	job->auxinfo_bit_align	= 0;
	job->auxinfo_dir	= 0;
	job->icv_len		= 0;

	switch (enc) {
	case CRYPTO_MODE_NULL:
		break;
	case CRYPTO_MODE_AES_ECB:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_AES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_ECB);
		break;
	case CRYPTO_MODE_AES_CBC:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_AES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CBC);
		break;

	case CRYPTO_MODE_AES_CS1:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_AES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CBC);
		job->auxinfo_cs_mode = 1;
		break;
	case CRYPTO_MODE_AES_CS2:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_AES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CBC);
		job->auxinfo_cs_mode = 2;
		break;
	case CRYPTO_MODE_AES_CS3:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_AES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CBC);
		job->auxinfo_cs_mode = 3;
		break;
	case CRYPTO_MODE_AES_CFB:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_AES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CFB);
		break;
	case CRYPTO_MODE_AES_OFB:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_AES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_OFB);
		break;
	case CRYPTO_MODE_AES_CTR:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_AES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CTR);
		break;
	case CRYPTO_MODE_AES_CCM:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_AES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CCM);
		break;
	case CRYPTO_MODE_AES_GCM:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_AES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_GCM);
		break;
	case CRYPTO_MODE_AES_F8:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_AES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_F8);
		break;
	case CRYPTO_MODE_AES_XTS:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_AES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_XTS);
		break;
	case CRYPTO_MODE_MULTI2_ECB:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_MULTI2);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_ECB);
		break;
	case CRYPTO_MODE_MULTI2_CBC:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_MULTI2);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CBC);
		break;
	case CRYPTO_MODE_MULTI2_OFB:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_MULTI2);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_OFB);
		break;
	case CRYPTO_MODE_MULTI2_CFB:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_MULTI2);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CFB);
		break;
	case CRYPTO_MODE_3DES_CBC:
	case CRYPTO_MODE_DES_CBC:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_DES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CBC);
		break;
	case CRYPTO_MODE_3DES_ECB:
	case CRYPTO_MODE_DES_ECB:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_DES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_ECB);
		break;
	case CRYPTO_MODE_KASUMI_ECB:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_KASUMI);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_ECB);
		break;
	case CRYPTO_MODE_KASUMI_F8:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_KASUMI);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_F8);
		break;
	case CRYPTO_MODE_SNOW3G_UEA2:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG,
				C_SNOW3G_UEA2);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_ECB);
		break;
	case CRYPTO_MODE_ZUC_UEA3:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG,
				C_ZUC_UEA3);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_ECB);
		break;
	case CRYPTO_MODE_CHACHA20_STREAM:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_CHACHA20);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CHACHA_STREAM);
		break;
	case CRYPTO_MODE_CHACHA20_POLY1305:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG,
				C_CHACHA20);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE,
				CM_CHACHA_AEAD);
		break;
	case CRYPTO_MODE_SM4_ECB:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_SM4);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_ECB);
		break;
	case CRYPTO_MODE_SM4_CBC:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_SM4);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CBC);
		break;
	case CRYPTO_MODE_SM4_CS1:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_SM4);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CBC);
		job->auxinfo_cs_mode = 1;
		break;
	case CRYPTO_MODE_SM4_CS2:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_SM4);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CBC);
		job->auxinfo_cs_mode = 2;
		break;
	case CRYPTO_MODE_SM4_CS3:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_SM4);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CBC);
		job->auxinfo_cs_mode = 3;
		break;
	case CRYPTO_MODE_SM4_CFB:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_SM4);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CFB);
		break;
	case CRYPTO_MODE_SM4_OFB:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_SM4);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_OFB);
		break;
	case CRYPTO_MODE_SM4_CTR:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_SM4);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CTR);
		break;
	case CRYPTO_MODE_SM4_CCM:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_SM4);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CCM);
		break;
	case CRYPTO_MODE_SM4_GCM:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_SM4);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_GCM);
		break;
	case CRYPTO_MODE_SM4_F8:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_SM4);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_F8);
		break;
	case CRYPTO_MODE_SM4_XTS:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_SM4);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_XTS);
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	switch (hash) {
	case CRYPTO_MODE_NULL:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_NULL);
		break;
	case CRYPTO_MODE_HMAC_SHA1:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_SHA1);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_HMAC);
		break;
	case CRYPTO_MODE_HMAC_MD5:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_MD5);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_HMAC);
		break;
	case CRYPTO_MODE_HMAC_SHA224:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_SHA224);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_HMAC);
		break;
	case CRYPTO_MODE_HMAC_SHA256:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_SHA256);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_HMAC);
		break;
	case CRYPTO_MODE_HMAC_SHA384:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_SHA384);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_HMAC);
		break;
	case CRYPTO_MODE_HMAC_SHA512:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_SHA512);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_HMAC);
		break;
	case CRYPTO_MODE_HMAC_SHA512_224:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SHA512_224);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_HMAC);
		break;
	case CRYPTO_MODE_HMAC_SHA512_256:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SHA512_256);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_HMAC);
		break;
	case CRYPTO_MODE_SSLMAC_MD5:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_MD5);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE,
				HM_SSLMAC);
		break;
	case CRYPTO_MODE_SSLMAC_SHA1:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_SHA1);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE,
				HM_SSLMAC);
		break;
	case CRYPTO_MODE_HASH_SHA1:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_SHA1);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_HASH_MD5:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_MD5);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_HASH_SHA224:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_SHA224);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_HASH_SHA256:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_SHA256);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_HASH_SHA384:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_SHA384);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_HASH_SHA512:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_SHA512);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_HASH_SHA512_224:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SHA512_224);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_HASH_SHA512_256:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SHA512_256);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_HASH_SHA3_224:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SHA3_224);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_HASH_SHA3_256:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SHA3_256);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_HASH_SHA3_384:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SHA3_384);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_HASH_SHA3_512:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SHA3_512);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_HASH_SHAKE128:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SHAKE128);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE,
				HM_SHAKE_SHAKE);
		break;
	case CRYPTO_MODE_HASH_SHAKE256:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SHAKE256);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE,
				HM_SHAKE_SHAKE);
		break;
	case CRYPTO_MODE_HASH_CSHAKE128:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SHAKE128);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE,
				HM_SHAKE_CSHAKE);
		break;
	case CRYPTO_MODE_HASH_CSHAKE256:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SHAKE256);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE,
				HM_SHAKE_CSHAKE);
		break;
	case CRYPTO_MODE_MAC_KMAC128:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SHAKE128);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE,
				HM_SHAKE_KMAC);
		break;
	case CRYPTO_MODE_MAC_KMAC256:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SHAKE256);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE,
				HM_SHAKE_KMAC); break;
	case CRYPTO_MODE_MAC_KMACXOF128:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SHAKE128);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE,
				HM_SHAKE_KMAC);
		/* auxinfo_dir reused to indicate XOF */
		job->auxinfo_dir = 1;
		break;
	case CRYPTO_MODE_MAC_KMACXOF256:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SHAKE256);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE,
				HM_SHAKE_KMAC);
		/* auxinfo_dir reused to indicate XOF */
		job->auxinfo_dir = 1;
		break;
	case CRYPTO_MODE_MAC_XCBC:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_XCBC);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_MAC_CMAC:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_CMAC);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_MAC_KASUMI_F9:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_KF9);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_MAC_SNOW3G_UIA2:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SNOW3G_UIA2);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_MAC_ZUC_UIA3:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_ZUC_UIA3);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_MAC_POLY1305:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_POLY1305);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_HASH_CRC32:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_CRC32_I3E802_3);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE,  HM_RAW);
		break;
	case CRYPTO_MODE_MAC_MICHAEL:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_MICHAEL);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_HASH_SM3:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_SM3);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_HMAC_SM3:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG, H_SM3);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_HMAC);
		break;
	case CRYPTO_MODE_MAC_SM4_XCBC:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SM4_XCBC_MAC);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	case CRYPTO_MODE_MAC_SM4_CMAC:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_ALG,
				H_SM4_CMAC);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_HASH_MODE, HM_RAW);
		break;
	default:
		ret = -EOPNOTSUPP;
	}
	ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_MSG_BEGIN) |
		SPACC_CTRL_MASK(SPACC_CTRL_MSG_END);

	if (ret != CRYPTO_OK) {
		spacc_job_release(spacc, job_idx);
	} else {
		ret		= job_idx;
		job->first_use	= 1;
		job->enc_mode	= enc;
		job->hash_mode	= hash;
		job->ckey_sz	= 0;
		job->hkey_sz	= 0;
		job->job_done	= 0;
		job->job_swid	= 0;
		job->job_secure	= !!secure_mode;

		job->auxinfo_bit_align = 0;
		job->job_err	= -EINPROGRESS;
		job->ctrl	= ctrl |
				  SPACC_CTRL_SET(SPACC_CTRL_CTX_IDX,
						 job->ctx_idx);
		job->cb		= cb;
		job->cbdata	= cbdata;
	}

	return ret;
}

static int spacc_xof_stringsize_autodetect(struct spacc_device *spacc)
{
	void *virt;
	dma_addr_t dma;
	struct pdu_ddt	ddt;
	int ss, alg, i, stat;
	unsigned long spacc_ctrl[2] = {0xF400B400, 0xF400D400};
	unsigned char buf[256];
	unsigned long buflen, rbuf;
	unsigned char test_str[6] = {0x01, 0x20, 0x54, 0x45, 0x53, 0x54};
	unsigned char md[2][16] = {
			 {0xc3, 0x6d, 0x0a, 0x88, 0xfa, 0x37, 0x4c, 0x9b,
			  0x44, 0x74, 0xeb, 0x00, 0x5f, 0xe8, 0xca, 0x25},
			 {0x68, 0x77, 0x04, 0x11, 0xf8, 0xe3, 0xb0, 0x1e,
			  0x0d, 0xbf, 0x71, 0x6a, 0xe9, 0x87, 0x1a, 0x0d}};

	virt = dma_alloc_coherent(get_ddt_device(), 256, &dma, GFP_KERNEL);
	if (!virt)
		return -EIO;

	if (pdu_ddt_init(&ddt, 1)) {
		dma_free_coherent(get_ddt_device(), 256, virt, dma);
		return -EIO;
	}
	pdu_ddt_add(&ddt, dma, 256);

	/* populate registers for jobs*/
	writel((uint32_t)ddt.phys, spacc->regmap + SPACC_REG_SRC_PTR);
	writel((uint32_t)ddt.phys, spacc->regmap + SPACC_REG_DST_PTR);

	writel(16, spacc->regmap + SPACC_REG_PROC_LEN);
	writel(16, spacc->regmap + SPACC_REG_PRE_AAD_LEN);
	writel(16, spacc->regmap + SPACC_REG_ICV_LEN);
	writel(6, spacc->regmap + SPACC_REG_KEY_SZ);
	writel(0, spacc->regmap + SPACC_REG_SW_CTRL);

	/* repeat for 2 algorithms, CSHAKE128 and KMAC128*/
	for (alg = 0; (alg < 2) && (spacc->config.string_size == 0); alg++) {
		/* repeat for 4 string_size sizes*/
		for (ss = 0; ss < 4; ss++) {
			buflen = (32UL << ss);
			if (buflen > spacc->config.hash_page_size)
				break;

			/* clear I/O memory*/
			memset(virt, 0, 256);

			/* clear buf and then insert test string*/
			memset(buf, 0, sizeof(buf));
			memcpy(buf, test_str, sizeof(test_str));
			memcpy(buf + (buflen >> 1), test_str, sizeof(test_str));

			/* write key context */
			pdu_to_dev_s(spacc->regmap + SPACC_CTX_HASH_KEY,
				     buf,
				     spacc->config.hash_page_size >> 2,
				     spacc->config.spacc_endian);

			/* write ctrl register */
			writel(spacc_ctrl[alg], spacc->regmap + SPACC_REG_CTRL);

			/* wait for job to complete */
			for (i = 0; i < 20; i++) {
				rbuf = 0;
				rbuf = readl(spacc->regmap +
					     SPACC_REG_FIFO_STAT) &
				       SPACC_FIFO_STAT_STAT_EMPTY;
				if (!rbuf) {
					/* check result, if it matches,
					 * we have string_size
					 */
					writel(1, spacc->regmap +
					       SPACC_REG_STAT_POP);
					rbuf = 0;
					rbuf = readl(spacc->regmap +
						     SPACC_REG_STATUS);
					stat = SPACC_GET_STATUS_RET_CODE(rbuf);
					if ((!memcmp(virt, md[alg], 16)) &&
					    stat == SPACC_OK) {
						spacc->config.string_size =
								(16 << ss);
					}
					break;
				}
			}
		}
	}

	/* reset registers */
	writel(0, spacc->regmap + SPACC_REG_IRQ_CTRL);
	writel(0, spacc->regmap + SPACC_REG_IRQ_EN);
	writel(0xFFFFFFFF, spacc->regmap + SPACC_REG_IRQ_STAT);

	writel(0, spacc->regmap + SPACC_REG_SRC_PTR);
	writel(0, spacc->regmap + SPACC_REG_DST_PTR);
	writel(0, spacc->regmap + SPACC_REG_PROC_LEN);
	writel(0, spacc->regmap + SPACC_REG_ICV_LEN);
	writel(0, spacc->regmap + SPACC_REG_PRE_AAD_LEN);

	pdu_ddt_free(&ddt);
	dma_free_coherent(get_ddt_device(), 256, virt, dma);

	return CRYPTO_OK;
}

/* free up the memory */
void spacc_fini(struct spacc_device *spacc)
{
	vfree(spacc->ctx);
	vfree(spacc->job);
}

int spacc_init(void __iomem *baseaddr, struct spacc_device *spacc,
	       struct pdu_info *info)
{
	unsigned long id;
	char version_string[3][16]  = { "SPACC", "SPACC-PDU" };
	char idx_string[2][16]      = { "(Normal Port)", "(Secure Port)" };
	char dma_type_string[4][16] = { "Unknown", "Scattergather", "Linear",
					"Unknown" };

	if (!baseaddr) {
		pr_debug("ERR: baseaddr is NULL\n");
		return -1;
	}
	if (!spacc) {
		pr_debug("ERR: spacc is NULL\n");
		return -1;
	}

	memset(spacc, 0, sizeof(*spacc));
	spin_lock_init(&spacc->lock);
	spin_lock_init(&spacc->ctx_lock);

	/* assign the baseaddr*/
	spacc->regmap = baseaddr;

	/* version info*/
	spacc->config.version     = info->spacc_version.version;
	spacc->config.pdu_version = (info->pdu_config.major << 4) |
				    info->pdu_config.minor;
	spacc->config.project     = info->spacc_version.project;
	spacc->config.is_pdu      = info->spacc_version.is_pdu;
	spacc->config.is_qos      = info->spacc_version.qos;

	/* misc*/
	spacc->config.is_partial        = info->spacc_version.partial;
	spacc->config.num_ctx           = info->spacc_config.num_ctx;
	spacc->config.ciph_page_size    = 1U <<
					  info->spacc_config.ciph_ctx_page_size;

	spacc->config.hash_page_size    = 1U <<
					  info->spacc_config.hash_ctx_page_size;

	spacc->config.dma_type          = info->spacc_config.dma_type;
	spacc->config.idx               = info->spacc_version.vspacc_idx;
	spacc->config.cmd0_fifo_depth   = info->spacc_config.cmd0_fifo_depth;
	spacc->config.cmd1_fifo_depth   = info->spacc_config.cmd1_fifo_depth;
	spacc->config.cmd2_fifo_depth   = info->spacc_config.cmd2_fifo_depth;
	spacc->config.stat_fifo_depth   = info->spacc_config.stat_fifo_depth;
	spacc->config.fifo_cnt          = 1;
	spacc->config.is_ivimport       = info->spacc_version.ivimport;

	/* ctrl register map*/
	if (spacc->config.version <= 0x4E)
		spacc->config.ctrl_map = spacc_ctrl_map[SPACC_CTRL_VER_0];
	else if (spacc->config.version <= 0x60)
		spacc->config.ctrl_map = spacc_ctrl_map[SPACC_CTRL_VER_1];
	else
		spacc->config.ctrl_map = spacc_ctrl_map[SPACC_CTRL_VER_2];

	spacc->job_next_swid   = 0;
	spacc->wdcnt           = 0;
	spacc->config.wd_timer = SPACC_WD_TIMER_INIT;

	/* version 4.10 uses IRQ,
	 * above uses WD and we don't support below 4.00
	 */
	if (spacc->config.version < 0x40) {
		pr_debug("ERR: Unsupported SPAcc version\n");
		return -EIO;
	} else if (spacc->config.version < 0x4B) {
		spacc->op_mode = SPACC_OP_MODE_IRQ;
	} else {
		spacc->op_mode = SPACC_OP_MODE_WD;
	}

	/* set threshold and enable irq
	 * on 4.11 and newer cores we can derive this
	 * from the HW reported depths.
	 */
	if (spacc->config.stat_fifo_depth == 1)
		spacc->config.ideal_stat_level = 1;
	else if (spacc->config.stat_fifo_depth <= 4)
		spacc->config.ideal_stat_level =
					spacc->config.stat_fifo_depth - 1;
	else if (spacc->config.stat_fifo_depth <= 8)
		spacc->config.ideal_stat_level =
					spacc->config.stat_fifo_depth - 2;
	else
		spacc->config.ideal_stat_level =
					spacc->config.stat_fifo_depth - 4;

	/* determine max PROClen value */
	writel(0xFFFFFFFF, spacc->regmap + SPACC_REG_PROC_LEN);
	spacc->config.max_msg_size = readl(spacc->regmap + SPACC_REG_PROC_LEN);

	/* read config info*/
	if (spacc->config.is_pdu) {
		pr_debug("PDU:\n");
		pr_debug("   MAJOR      : %u\n", info->pdu_config.major);
		pr_debug("   MINOR      : %u\n", info->pdu_config.minor);
	}

	id = readl(spacc->regmap + SPACC_REG_ID);
	pr_debug("SPACC ID: (%08lx)\n", (unsigned long)id);
	pr_debug("   MAJOR      : %x\n", info->spacc_version.major);
	pr_debug("   MINOR      : %x\n", info->spacc_version.minor);
	pr_debug("   QOS        : %x\n", info->spacc_version.qos);
	pr_debug("   IVIMPORT   : %x\n", spacc->config.is_ivimport);

	if (spacc->config.version >= 0x48)
		pr_debug("   TYPE       : %lx (%s)\n", SPACC_ID_TYPE(id),
			version_string[SPACC_ID_TYPE(id) & 3]);

	pr_debug("   AUX        : %x\n", info->spacc_version.qos);
	pr_debug("   IDX        : %lx %s\n", SPACC_ID_VIDX(id),
			spacc->config.is_secure ?
			(idx_string[spacc->config.is_secure_port & 1]) : "");
	pr_debug("   PARTIAL    : %x\n", info->spacc_version.partial);
	pr_debug("   PROJECT    : %x\n", info->spacc_version.project);

	if (spacc->config.version >= 0x48)
		id = readl(spacc->regmap + SPACC_REG_CONFIG);
	else
		id = 0xFFFFFFFF;

	pr_debug("SPACC CFG: (%08lx)\n", id);
	pr_debug("   CTX CNT    : %u\n", info->spacc_config.num_ctx);
	pr_debug("   VSPACC CNT : %u\n", info->spacc_config.num_vspacc);
	pr_debug("   CIPH SZ    : %-3lu bytes\n", 1UL <<
				  info->spacc_config.ciph_ctx_page_size);
	pr_debug("   HASH SZ    : %-3lu bytes\n", 1UL <<
				  info->spacc_config.hash_ctx_page_size);
	pr_debug("   DMA TYPE   : %u (%s)\n", info->spacc_config.dma_type,
			dma_type_string[info->spacc_config.dma_type & 3]);
	pr_debug("   MAX PROCLEN: %lu bytes\n", (unsigned long)
				  spacc->config.max_msg_size);
	pr_debug("   FIFO CONFIG :\n");
	pr_debug("      CMD0 DEPTH: %d\n", spacc->config.cmd0_fifo_depth);

	if (spacc->config.is_qos) {
		pr_debug("      CMD1 DEPTH: %d\n",
				spacc->config.cmd1_fifo_depth);
		pr_debug("      CMD2 DEPTH: %d\n",
				spacc->config.cmd2_fifo_depth);
	}
	pr_debug("      STAT DEPTH: %d\n", spacc->config.stat_fifo_depth);

	if (spacc->config.dma_type == SPACC_DMA_DDT) {
		writel(0x1234567F, baseaddr + SPACC_REG_DST_PTR);
		writel(0xDEADBEEF, baseaddr + SPACC_REG_SRC_PTR);

		if (((readl(baseaddr + SPACC_REG_DST_PTR)) !=
					(0x1234567F & SPACC_DST_PTR_PTR)) ||
		    ((readl(baseaddr + SPACC_REG_SRC_PTR)) !=
		     (0xDEADBEEF & SPACC_SRC_PTR_PTR))) {
			pr_debug("ERR: Failed to set pointers\n");
			goto ERR;
		}
	}

	/* zero the IRQ CTRL/EN register
	 * (to make sure we're in a sane state)
	 */
	writel(0, spacc->regmap + SPACC_REG_IRQ_CTRL);
	writel(0, spacc->regmap + SPACC_REG_IRQ_EN);
	writel(0xFFFFFFFF, spacc->regmap + SPACC_REG_IRQ_STAT);

	/* init cache*/
	memset(&spacc->cache, 0, sizeof(spacc->cache));
	writel(0, spacc->regmap + SPACC_REG_SRC_PTR);
	writel(0, spacc->regmap + SPACC_REG_DST_PTR);
	writel(0, spacc->regmap + SPACC_REG_PROC_LEN);
	writel(0, spacc->regmap + SPACC_REG_ICV_LEN);
	writel(0, spacc->regmap + SPACC_REG_ICV_OFFSET);
	writel(0, spacc->regmap + SPACC_REG_PRE_AAD_LEN);
	writel(0, spacc->regmap + SPACC_REG_POST_AAD_LEN);
	writel(0, spacc->regmap + SPACC_REG_IV_OFFSET);
	writel(0, spacc->regmap + SPACC_REG_OFFSET);
	writel(0, spacc->regmap + SPACC_REG_AUX_INFO);

	spacc->ctx = vmalloc(sizeof(struct spacc_ctx) * spacc->config.num_ctx);
	if (!spacc->ctx)
		goto ERR;

	spacc->job = vmalloc(sizeof(struct spacc_job) * SPACC_MAX_JOBS);
	if (!spacc->job)
		goto ERR;

	/* initialize job_idx and lookup table */
	spacc_job_init_all(spacc);

	/* initialize contexts */
	spacc_ctx_init_all(spacc);

	/* autodetect and set string size setting*/
	if (spacc->config.version == 0x61 || spacc->config.version >= 0x65)
		spacc_xof_stringsize_autodetect(spacc);

	return CRYPTO_OK;
ERR:
	spacc_fini(spacc);
	pr_debug("ERR: Crypto Failed\n");

	return -EIO;
}

/* callback function to initialize tasklet running */
void spacc_pop_jobs(unsigned long data)
{
	int num = 0;
	struct spacc_priv *priv =  (struct spacc_priv *)data;
	struct spacc_device *spacc = &priv->spacc;

	/* decrement the WD CNT here since
	 * now we're actually going to respond
	 * to the IRQ completely
	 */
	if (spacc->wdcnt)
		--(spacc->wdcnt);

	spacc_pop_packets(spacc, &num);
}

int spacc_remove(struct platform_device *pdev)
{
	struct spacc_device *spacc;
	struct spacc_priv *priv = platform_get_drvdata(pdev);

	/* free test vector memory*/
	spacc = &priv->spacc;
	spacc_fini(spacc);

	tasklet_kill(&priv->pop_jobs);

	/* devm functions do proper cleanup */
	pdu_mem_deinit(&pdev->dev);
	dev_dbg(&pdev->dev, "removed!\n");

	return 0;
}

int spacc_set_key_exp(struct spacc_device *spacc, int job_idx)
{
	struct spacc_ctx *ctx = NULL;
	struct spacc_job *job = NULL;

	if (job_idx < 0 || job_idx > SPACC_MAX_JOBS) {
		pr_debug("ERR: Invalid Job id specified (out of range)\n");
		return -ENXIO;
	}

	job = &spacc->job[job_idx];
	ctx = context_lookup_by_job(spacc, job_idx);

	if (!ctx) {
		pr_debug("ERR: Failed to find ctx id\n");
		return -EIO;
	}

	job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_KEY_EXP);

	return CRYPTO_OK;
}

int spacc_compute_xcbc_key(struct spacc_device *spacc, int mode_id,
			   int job_idx, const unsigned char *key,
			   int keylen, unsigned char *xcbc_out)
{
	unsigned char *buf;
	dma_addr_t bufphys;
	struct pdu_ddt ddt;
	unsigned char iv[16];
	int err, i, handle, usecbc, ctx_idx;

	if (job_idx >= 0 && job_idx < SPACC_MAX_JOBS)
		ctx_idx = spacc->job[job_idx].ctx_idx;
	else
		ctx_idx = -1;

	if (mode_id == CRYPTO_MODE_MAC_XCBC) {
		/* figure out if we can schedule the key  */
		if (spacc_isenabled(spacc, CRYPTO_MODE_AES_ECB, 16))
			usecbc = 0;
		else if (spacc_isenabled(spacc, CRYPTO_MODE_AES_CBC, 16))
			usecbc = 1;
		else
			return -1;
	} else if (mode_id == CRYPTO_MODE_MAC_SM4_XCBC) {
		/* figure out if we can schedule the key  */
		if (spacc_isenabled(spacc, CRYPTO_MODE_SM4_ECB, 16))
			usecbc = 0;
		else if (spacc_isenabled(spacc, CRYPTO_MODE_SM4_CBC, 16))
			usecbc = 1;
		else
			return -1;
	} else {
		return -1;
	}

	memset(iv, 0, sizeof(iv));
	memset(&ddt, 0, sizeof(ddt));

	buf = dma_alloc_coherent(get_ddt_device(), 64, &bufphys, GFP_KERNEL);
	if (!buf)
		return -EINVAL;

	handle = -1;

	/* set to 1111...., 2222...., 333... */
	for (i = 0; i < 48; i++)
		buf[i] = (i >> 4) + 1;

	/* build DDT */
	err = pdu_ddt_init(&ddt, 1);
	if (err)
		goto xcbc_err;

	pdu_ddt_add(&ddt, bufphys, 48);

	/* open a handle in either CBC or ECB mode */
	if (mode_id == CRYPTO_MODE_MAC_XCBC) {
		handle = spacc_open(spacc, (usecbc ?
				    CRYPTO_MODE_AES_CBC : CRYPTO_MODE_AES_ECB),
				    CRYPTO_MODE_NULL, ctx_idx, 0, NULL, NULL);
		if (handle < 0) {
			err = handle;
			goto xcbc_err;
		}
	} else if (mode_id == CRYPTO_MODE_MAC_SM4_XCBC) {
		handle = spacc_open(spacc, (usecbc ?
				    CRYPTO_MODE_SM4_CBC : CRYPTO_MODE_SM4_ECB),
				    CRYPTO_MODE_NULL, ctx_idx, 0, NULL, NULL);
		if (handle < 0) {
			err = handle;
			goto xcbc_err;
		}
	}
	spacc_set_operation(spacc, handle, OP_ENCRYPT, 0, 0, 0, 0, 0);

	if (usecbc) {
		/* we can do the ECB work in CBC using three
		 * jobs with the IVreset to zero each time
		 */
		for (i = 0; i < 3; i++) {
			spacc_write_context(spacc, handle,
					    SPACC_CRYPTO_OPERATION, key,
					    keylen, iv, 16);
			err = spacc_packet_enqueue_ddt(spacc, handle, &ddt,
						&ddt, 16, (i * 16) |
						((i * 16) << 16), 0, 0, 0, 0);
			if (err != CRYPTO_OK)
				goto xcbc_err;

			do {
				err = spacc_packet_dequeue(spacc, handle);
			} while (err == -EINPROGRESS);
			if (err != CRYPTO_OK)
				goto xcbc_err;
		}
	} else {
		/* do the 48 bytes as a single SPAcc job this is the ideal case
		 * but only possible if ECB was enabled in the core
		 */
		spacc_write_context(spacc, handle, SPACC_CRYPTO_OPERATION,
				    key, keylen, iv, 16);
		err = spacc_packet_enqueue_ddt(spacc, handle, &ddt, &ddt, 48,
					       0, 0, 0, 0, 0);
		if (err != CRYPTO_OK)
			goto xcbc_err;

		do {
			err = spacc_packet_dequeue(spacc, handle);
		} while (err == -EINPROGRESS);
		if (err != CRYPTO_OK)
			goto xcbc_err;
	}

	/* now we can copy the key*/
	memcpy(xcbc_out, buf, 48);
	memset(buf, 0, 64);

xcbc_err:
	dma_free_coherent(get_ddt_device(), 64, buf, bufphys);
	pdu_ddt_free(&ddt);
	if (handle >= 0)
		spacc_close(spacc, handle);

	if (err)
		return -EINVAL;

	return 0;
}
