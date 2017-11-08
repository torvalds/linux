/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MV_CRYPTO_H__
#define __MV_CRYPTO_H__

#define DIGEST_INITIAL_VAL_A	0xdd00
#define DIGEST_INITIAL_VAL_B	0xdd04
#define DIGEST_INITIAL_VAL_C	0xdd08
#define DIGEST_INITIAL_VAL_D	0xdd0c
#define DIGEST_INITIAL_VAL_E	0xdd10
#define DES_CMD_REG		0xdd58

#define SEC_ACCEL_CMD		0xde00
#define SEC_CMD_EN_SEC_ACCL0	(1 << 0)
#define SEC_CMD_EN_SEC_ACCL1	(1 << 1)
#define SEC_CMD_DISABLE_SEC	(1 << 2)

#define SEC_ACCEL_DESC_P0	0xde04
#define SEC_DESC_P0_PTR(x)	(x)

#define SEC_ACCEL_DESC_P1	0xde14
#define SEC_DESC_P1_PTR(x)	(x)

#define SEC_ACCEL_CFG		0xde08
#define SEC_CFG_STOP_DIG_ERR	(1 << 0)
#define SEC_CFG_CH0_W_IDMA	(1 << 7)
#define SEC_CFG_CH1_W_IDMA	(1 << 8)
#define SEC_CFG_ACT_CH0_IDMA	(1 << 9)
#define SEC_CFG_ACT_CH1_IDMA	(1 << 10)

#define SEC_ACCEL_STATUS	0xde0c
#define SEC_ST_ACT_0		(1 << 0)
#define SEC_ST_ACT_1		(1 << 1)

/*
 * FPGA_INT_STATUS looks like a FPGA leftover and is documented only in Errata
 * 4.12. It looks like that it was part of an IRQ-controller in FPGA and
 * someone forgot to remove  it while switching to the core and moving to
 * SEC_ACCEL_INT_STATUS.
 */
#define FPGA_INT_STATUS		0xdd68
#define SEC_ACCEL_INT_STATUS	0xde20
#define SEC_INT_AUTH_DONE	(1 << 0)
#define SEC_INT_DES_E_DONE	(1 << 1)
#define SEC_INT_AES_E_DONE	(1 << 2)
#define SEC_INT_AES_D_DONE	(1 << 3)
#define SEC_INT_ENC_DONE	(1 << 4)
#define SEC_INT_ACCEL0_DONE	(1 << 5)
#define SEC_INT_ACCEL1_DONE	(1 << 6)
#define SEC_INT_ACC0_IDMA_DONE	(1 << 7)
#define SEC_INT_ACC1_IDMA_DONE	(1 << 8)

#define SEC_ACCEL_INT_MASK	0xde24

#define AES_KEY_LEN	(8 * 4)

struct sec_accel_config {

	u32 config;
#define CFG_OP_MAC_ONLY		0
#define CFG_OP_CRYPT_ONLY	1
#define CFG_OP_MAC_CRYPT	2
#define CFG_OP_CRYPT_MAC	3
#define CFG_MACM_MD5		(4 << 4)
#define CFG_MACM_SHA1		(5 << 4)
#define CFG_MACM_HMAC_MD5	(6 << 4)
#define CFG_MACM_HMAC_SHA1	(7 << 4)
#define CFG_ENCM_DES		(1 << 8)
#define CFG_ENCM_3DES		(2 << 8)
#define CFG_ENCM_AES		(3 << 8)
#define CFG_DIR_ENC		(0 << 12)
#define CFG_DIR_DEC		(1 << 12)
#define CFG_ENC_MODE_ECB	(0 << 16)
#define CFG_ENC_MODE_CBC	(1 << 16)
#define CFG_3DES_EEE		(0 << 20)
#define CFG_3DES_EDE		(1 << 20)
#define CFG_AES_LEN_128		(0 << 24)
#define CFG_AES_LEN_192		(1 << 24)
#define CFG_AES_LEN_256		(2 << 24)
#define CFG_NOT_FRAG		(0 << 30)
#define CFG_FIRST_FRAG		(1 << 30)
#define CFG_LAST_FRAG		(2 << 30)
#define CFG_MID_FRAG		(3 << 30)

	u32 enc_p;
#define ENC_P_SRC(x)		(x)
#define ENC_P_DST(x)		((x) << 16)

	u32 enc_len;
#define ENC_LEN(x)		(x)

	u32 enc_key_p;
#define ENC_KEY_P(x)		(x)

	u32 enc_iv;
#define ENC_IV_POINT(x)		((x) << 0)
#define ENC_IV_BUF_POINT(x)	((x) << 16)

	u32 mac_src_p;
#define MAC_SRC_DATA_P(x)	(x)
#define MAC_SRC_TOTAL_LEN(x)	((x) << 16)

	u32 mac_digest;
#define MAC_DIGEST_P(x)	(x)
#define MAC_FRAG_LEN(x)	((x) << 16)
	u32 mac_iv;
#define MAC_INNER_IV_P(x)	(x)
#define MAC_OUTER_IV_P(x)	((x) << 16)
}__attribute__ ((packed));
	/*
	 * /-----------\ 0
	 * | ACCEL CFG |	4 * 8
	 * |-----------| 0x20
	 * | CRYPT KEY |	8 * 4
	 * |-----------| 0x40
	 * |  IV   IN  |	4 * 4
	 * |-----------| 0x40 (inplace)
	 * |  IV BUF   |	4 * 4
	 * |-----------| 0x80
	 * |  DATA IN  |	16 * x (max ->max_req_size)
	 * |-----------| 0x80 (inplace operation)
	 * |  DATA OUT |	16 * x (max ->max_req_size)
	 * \-----------/ SRAM size
	 */

	/* Hashing memory map:
	 * /-----------\ 0
	 * | ACCEL CFG |        4 * 8
	 * |-----------| 0x20
	 * | Inner IV  |        5 * 4
	 * |-----------| 0x34
	 * | Outer IV  |        5 * 4
	 * |-----------| 0x48
	 * | Output BUF|        5 * 4
	 * |-----------| 0x80
	 * |  DATA IN  |        64 * x (max ->max_req_size)
	 * \-----------/ SRAM size
	 */
#define SRAM_CONFIG		0x00
#define SRAM_DATA_KEY_P		0x20
#define SRAM_DATA_IV		0x40
#define SRAM_DATA_IV_BUF	0x40
#define SRAM_DATA_IN_START	0x80
#define SRAM_DATA_OUT_START	0x80

#define SRAM_HMAC_IV_IN		0x20
#define SRAM_HMAC_IV_OUT	0x34
#define SRAM_DIGEST_BUF		0x48

#define SRAM_CFG_SPACE		0x80

#endif
