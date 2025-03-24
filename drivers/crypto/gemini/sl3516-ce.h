/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sl3516-ce.h - hardware cryptographic offloader for cortina/gemini SoC
 *
 * Copyright (C) 2021 Corentin LABBE <clabbe@baylibre.com>
 *
 * General notes on this driver:
 * Called either Crypto Acceleration Engine Module, Security Acceleration Engine
 * or IPSEC module in the datasheet, it will be called Crypto Engine for short
 * in this driver.
 * The CE was designed to handle IPSEC and wifi(TKIP WEP) protocol.
 * It can handle AES, DES, 3DES, MD5, WEP, TKIP, SHA1, HMAC(MD5), HMAC(SHA1),
 * Michael cipher/digest suites.
 * It acts the same as a network hw, with both RX and TX chained descriptors.
 */
#include <crypto/aes.h>
#include <crypto/engine.h>
#include <crypto/scatterwalk.h>
#include <crypto/skcipher.h>
#include <linux/debugfs.h>
#include <linux/hw_random.h>

#define TQ0_TYPE_DATA 0
#define TQ0_TYPE_CTRL BIT(0)
#define TQ1_CIPHER BIT(1)
#define TQ2_AUTH BIT(2)
#define TQ3_IV BIT(3)
#define TQ4_KEY0 BIT(4)
#define TQ5_KEY4 BIT(5)
#define TQ6_KEY6 BIT(6)
#define TQ7_AKEY0 BIT(7)
#define TQ8_AKEY2 BIT(8)
#define TQ9_AKEY2 BIT(9)

#define ECB_AES       0x2

#define DESC_LAST 0x01
#define DESC_FIRST 0x02

#define IPSEC_ID		0x0000
#define IPSEC_STATUS_REG	0x00a8
#define IPSEC_RAND_NUM_REG	0x00ac
#define IPSEC_DMA_DEVICE_ID	0xff00
#define IPSEC_DMA_STATUS	0xff04
#define IPSEC_TXDMA_CTRL	0xff08
#define IPSEC_TXDMA_FIRST_DESC	0xff0c
#define IPSEC_TXDMA_CURR_DESC	0xff10
#define IPSEC_RXDMA_CTRL	0xff14
#define IPSEC_RXDMA_FIRST_DESC	0xff18
#define IPSEC_RXDMA_CURR_DESC	0xff1c
#define IPSEC_TXDMA_BUF_ADDR	0xff28
#define IPSEC_RXDMA_BUF_ADDR	0xff38
#define IPSEC_RXDMA_BUF_SIZE	0xff30

#define CE_ENCRYPTION		0x01
#define CE_DECRYPTION		0x03

#define MAXDESC 6

#define DMA_STATUS_RS_EOFI	BIT(22)
#define DMA_STATUS_RS_PERR	BIT(24)
#define DMA_STATUS_RS_DERR	BIT(25)
#define DMA_STATUS_TS_EOFI	BIT(27)
#define DMA_STATUS_TS_PERR	BIT(29)
#define DMA_STATUS_TS_DERR	BIT(30)

#define TXDMA_CTRL_START BIT(31)
#define TXDMA_CTRL_CONTINUE BIT(30)
#define TXDMA_CTRL_CHAIN_MODE BIT(29)
/* the burst value is not documented in the datasheet */
#define TXDMA_CTRL_BURST_UNK BIT(22)
#define TXDMA_CTRL_INT_FAIL BIT(17)
#define TXDMA_CTRL_INT_PERR BIT(16)

#define RXDMA_CTRL_START BIT(31)
#define RXDMA_CTRL_CONTINUE BIT(30)
#define RXDMA_CTRL_CHAIN_MODE BIT(29)
/* the burst value is not documented in the datasheet */
#define RXDMA_CTRL_BURST_UNK BIT(22)
#define RXDMA_CTRL_INT_FINISH BIT(18)
#define RXDMA_CTRL_INT_FAIL BIT(17)
#define RXDMA_CTRL_INT_PERR BIT(16)
#define RXDMA_CTRL_INT_EOD BIT(15)
#define RXDMA_CTRL_INT_EOF BIT(14)

#define CE_CPU 0
#define CE_DMA 1

/*
 * struct sl3516_ce_descriptor - descriptor for CE operations
 * @frame_ctrl:		Information for the current descriptor
 * @flag_status:	For send packet, describe flag of operations.
 * @buf_adr:		pointer to a send/recv buffer for data packet
 * @next_desc:		control linking to other descriptors
 */
struct descriptor {
	union {
		u32 raw;
		/*
		 * struct desc_frame_ctrl - Information for the current descriptor
		 * @buffer_size:	the size of buffer at buf_adr
		 * @desc_count:		Upon completion of a DMA operation, DMA
		 *			write the number of descriptors used
		 *			for the current frame
		 * @checksum:		unknown
		 * @authcomp:		unknown
		 * @perr:		Protocol error during processing this descriptor
		 * @derr:		Data error during processing this descriptor
		 * @own:		0 if owned by CPU, 1 for DMA
		 */
		struct desc_frame_ctrl {
			u32 buffer_size	:16;
			u32 desc_count	:6;
			u32 checksum	:6;
			u32 authcomp	:1;
			u32 perr	:1;
			u32 derr	:1;
			u32 own		:1;
		} bits;
	} frame_ctrl;

	union {
		u32 raw;
		/*
		 * struct desc_flag_status - flag for this descriptor
		 * @tqflag:	list of flag describing the type of operation
		 *		to be performed.
		 */
		struct desc_tx_flag_status {
			u32 tqflag	:10;
			u32 unused	:22;
		} tx_flag;
	} flag_status;

	u32 buf_adr;

	union {
		u32 next_descriptor;
		/*
		 * struct desc_next - describe chaining of descriptors
		 * @sof_eof:	does the descriptor is first (0x11),
		 *		the last (0x01), middle of a chan (0x00)
		 *		or the only one (0x11)
		 * @dec:	AHB bus address increase (0), decrease (1)
		 * @eofie:	End of frame interrupt enable
		 * @ndar:	Next descriptor address
		 */
		struct desc_next {
			u32 sof_eof	:2;
			u32 dec		:1;
			u32 eofie	:1;
			u32 ndar	:28;
		} bits;
	} next_desc;
};

/*
 * struct control - The value of this register is used to set the
 *			operation mode of the IPSec Module.
 * @process_id:		Used to identify the process. The number will be copied
 *			to the descriptor status of the received packet.
 * @auth_check_len:	Number of 32-bit words to be checked or appended by the
 *			authentication module
 * @auth_algorithm:
 * @auth_mode:		0:append 1:Check Authentication Result
 * @fcs_stream_copy:	0:enable 1:disable authentication stream copy
 * @mix_key_sel:	0:use rCipherKey0-3  1:use Key Mixer
 * @aesnk:		AES Key Size
 * @cipher_algorithm:	choice of CBC/ECE and AES/DES/3DES
 * @op_mode:		Operation Mode for the IPSec Module
 */
struct pkt_control_header {
	u32 process_id		:8;
	u32 auth_check_len	:3;
	u32 un1			:1;
	u32 auth_algorithm	:3;
	u32 auth_mode		:1;
	u32 fcs_stream_copy	:1;
	u32 un2			:2;
	u32 mix_key_sel		:1;
	u32 aesnk		:4;
	u32 cipher_algorithm	:3;
	u32 un3			:1;
	u32 op_mode		:4;
};

struct pkt_control_cipher {
	u32 algorithm_len	:16;
	u32 header_len		:16;
};

/*
 * struct pkt_control_ecb - control packet for ECB
 */
struct pkt_control_ecb {
	struct pkt_control_header control;
	struct pkt_control_cipher cipher;
	unsigned char key[AES_MAX_KEY_SIZE];
};

/*
 * struct sl3516_ce_dev - main container for all this driver information
 * @base:	base address
 * @clks:	clocks used
 * @reset:	pointer to reset controller
 * @dev:	the platform device
 * @engine:	ptr to the crypto/crypto_engine
 * @complete:	completion for the current task on this flow
 * @status:	set to 1 by interrupt if task is done
 * @dtx:	base DMA address for TX descriptors
 * @tx		base address of TX descriptors
 * @drx:	base DMA address for RX descriptors
 * @rx		base address of RX descriptors
 * @ctx		current used TX descriptor
 * @crx		current used RX descriptor
 * @trng	hw_random structure for RNG
 * @hwrng_stat_req	number of HWRNG requests
 * @hwrng_stat_bytes	total number of bytes generated by RNG
 * @stat_irq	number of IRQ handled by CE
 * @stat_irq_tx	number of TX IRQ handled by CE
 * @stat_irq_rx	number of RX IRQ handled by CE
 * @stat_req	number of requests handled by CE
 * @fallbak_sg_count_tx		number of fallback due to destination SG count
 * @fallbak_sg_count_rx		number of fallback due to source SG count
 * @fallbak_not_same_len	number of fallback due to difference in SG length
 * @dbgfs_dir:	Debugfs dentry for statistic directory
 * @dbgfs_stats: Debugfs dentry for statistic counters
 */
struct sl3516_ce_dev {
	void __iomem *base;
	struct clk *clks;
	struct reset_control *reset;
	struct device *dev;
	struct crypto_engine *engine;
	struct completion complete;
	int status;
	dma_addr_t dtx;
	struct descriptor *tx;
	dma_addr_t drx;
	struct descriptor *rx;
	int ctx;
	int crx;
	struct hwrng trng;
	unsigned long hwrng_stat_req;
	unsigned long hwrng_stat_bytes;
	unsigned long stat_irq;
	unsigned long stat_irq_tx;
	unsigned long stat_irq_rx;
	unsigned long stat_req;
	unsigned long fallback_sg_count_tx;
	unsigned long fallback_sg_count_rx;
	unsigned long fallback_not_same_len;
	unsigned long fallback_mod16;
	unsigned long fallback_align16;
#ifdef CONFIG_CRYPTO_DEV_SL3516_DEBUG
	struct dentry *dbgfs_dir;
	struct dentry *dbgfs_stats;
#endif
	void *pctrl;
	dma_addr_t dctrl;
};

struct sginfo {
	u32 addr;
	u32 len;
};

/*
 * struct sl3516_ce_cipher_req_ctx - context for a skcipher request
 * @t_src:		list of mapped SGs with their size
 * @t_dst:		list of mapped SGs with their size
 * @op_dir:		direction (encrypt vs decrypt) for this request
 * @pctrllen:		the length of the ctrl packet
 * @tqflag:		the TQflag to set in data packet
 * @h			pointer to the pkt_control_cipher header
 * @nr_sgs:		number of source SG
 * @nr_sgd:		number of destination SG
 * @fallback_req:	request struct for invoking the fallback skcipher TFM
 */
struct sl3516_ce_cipher_req_ctx {
	struct sginfo t_src[MAXDESC];
	struct sginfo t_dst[MAXDESC];
	u32 op_dir;
	unsigned int pctrllen;
	u32 tqflag;
	struct pkt_control_cipher *h;
	int nr_sgs;
	int nr_sgd;
	struct skcipher_request fallback_req;   // keep at the end
};

/*
 * struct sl3516_ce_cipher_tfm_ctx - context for a skcipher TFM
 * @key:		pointer to key data
 * @keylen:		len of the key
 * @ce:			pointer to the private data of driver handling this TFM
 * @fallback_tfm:	pointer to the fallback TFM
 */
struct sl3516_ce_cipher_tfm_ctx {
	u32 *key;
	u32 keylen;
	struct sl3516_ce_dev *ce;
	struct crypto_skcipher *fallback_tfm;
};

/*
 * struct sl3516_ce_alg_template - crypto_alg template
 * @type:		the CRYPTO_ALG_TYPE for this template
 * @mode:		value to be used in control packet for this algorithm
 * @ce:			pointer to the sl3516_ce_dev structure associated with
 *			this template
 * @alg:		one of sub struct must be used
 * @stat_req:		number of request done on this template
 * @stat_fb:		number of request which has fallbacked
 * @stat_bytes:		total data size done by this template
 */
struct sl3516_ce_alg_template {
	u32 type;
	u32 mode;
	struct sl3516_ce_dev *ce;
	union {
		struct skcipher_engine_alg skcipher;
	} alg;
	unsigned long stat_req;
	unsigned long stat_fb;
	unsigned long stat_bytes;
};

int sl3516_ce_aes_setkey(struct crypto_skcipher *tfm, const u8 *key,
			 unsigned int keylen);
int sl3516_ce_cipher_init(struct crypto_tfm *tfm);
void sl3516_ce_cipher_exit(struct crypto_tfm *tfm);
int sl3516_ce_skdecrypt(struct skcipher_request *areq);
int sl3516_ce_skencrypt(struct skcipher_request *areq);

int sl3516_ce_run_task(struct sl3516_ce_dev *ce,
		       struct sl3516_ce_cipher_req_ctx *rctx, const char *name);

int sl3516_ce_rng_register(struct sl3516_ce_dev *ce);
void sl3516_ce_rng_unregister(struct sl3516_ce_dev *ce);
int sl3516_ce_handle_cipher_request(struct crypto_engine *engine, void *areq);
