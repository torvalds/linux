// SPDX-License-Identifier: GPL-2.0
//
// Register map access API - SPI AVMM support
//
// Copyright (C) 2018-2020 Intel Corporation. All rights reserved.

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/swab.h>

/*
 * This driver implements the regmap operations for a generic SPI
 * master to access the registers of the spi slave chip which has an
 * Avalone bus in it.
 *
 * The "SPI slave to Avalon Master Bridge" (spi-avmm) IP should be integrated
 * in the spi slave chip. The IP acts as a bridge to convert encoded streams of
 * bytes from the host to the internal register read/write on Avalon bus. In
 * order to issue register access requests to the slave chip, the host should
 * send formatted bytes that conform to the transfer protocol.
 * The transfer protocol contains 3 layers: transaction layer, packet layer
 * and physical layer.
 *
 * Reference Documents could be found at:
 * https://www.intel.com/content/www/us/en/programmable/documentation/sfo1400787952932.html
 *
 * Chapter "SPI Slave/JTAG to Avalon Master Bridge Cores" is a general
 * introduction to the protocol.
 *
 * Chapter "Avalon Packets to Transactions Converter Core" describes
 * the transaction layer.
 *
 * Chapter "Avalon-ST Bytes to Packets and Packets to Bytes Converter Cores"
 * describes the packet layer.
 *
 * Chapter "Avalon-ST Serial Peripheral Interface Core" describes the
 * physical layer.
 *
 *
 * When host issues a regmap read/write, the driver will transform the request
 * to byte stream layer by layer. It formats the register addr, value and
 * length to the transaction layer request, then converts the request to packet
 * layer bytes stream and then to physical layer bytes stream. Finally the
 * driver sends the formatted byte stream over SPI bus to the slave chip.
 *
 * The spi-avmm IP on the slave chip decodes the byte stream and initiates
 * register read/write on its internal Avalon bus, and then encodes the
 * response to byte stream and sends back to host.
 *
 * The driver receives the byte stream, reverses the 3 layers transformation,
 * and finally gets the response value (read out data for register read,
 * successful written size for register write).
 */

#define PKT_SOP			0x7a
#define PKT_EOP			0x7b
#define PKT_CHANNEL		0x7c
#define PKT_ESC			0x7d

#define PHY_IDLE		0x4a
#define PHY_ESC			0x4d

#define TRANS_CODE_WRITE	0x0
#define TRANS_CODE_SEQ_WRITE	0x4
#define TRANS_CODE_READ		0x10
#define TRANS_CODE_SEQ_READ	0x14
#define TRANS_CODE_NO_TRANS	0x7f

#define SPI_AVMM_XFER_TIMEOUT	(msecs_to_jiffies(200))

/* slave's register addr is 32 bits */
#define SPI_AVMM_REG_SIZE		4UL
/* slave's register value is 32 bits */
#define SPI_AVMM_VAL_SIZE		4UL

/*
 * max rx size could be larger. But considering the buffer consuming,
 * it is proper that we limit 1KB xfer at max.
 */
#define MAX_READ_CNT		256UL
#define MAX_WRITE_CNT		1UL

struct trans_req_header {
	u8 code;
	u8 rsvd;
	__be16 size;
	__be32 addr;
} __packed;

struct trans_resp_header {
	u8 r_code;
	u8 rsvd;
	__be16 size;
} __packed;

#define TRANS_REQ_HD_SIZE	(sizeof(struct trans_req_header))
#define TRANS_RESP_HD_SIZE	(sizeof(struct trans_resp_header))

/*
 * In transaction layer,
 * the write request format is: Transaction request header + data
 * the read request format is: Transaction request header
 * the write response format is: Transaction response header
 * the read response format is: pure data, no Transaction response header
 */
#define TRANS_WR_TX_SIZE(n)	(TRANS_REQ_HD_SIZE + SPI_AVMM_VAL_SIZE * (n))
#define TRANS_RD_TX_SIZE	TRANS_REQ_HD_SIZE
#define TRANS_TX_MAX		TRANS_WR_TX_SIZE(MAX_WRITE_CNT)

#define TRANS_RD_RX_SIZE(n)	(SPI_AVMM_VAL_SIZE * (n))
#define TRANS_WR_RX_SIZE	TRANS_RESP_HD_SIZE
#define TRANS_RX_MAX		TRANS_RD_RX_SIZE(MAX_READ_CNT)

/* tx & rx share one transaction layer buffer */
#define TRANS_BUF_SIZE		((TRANS_TX_MAX > TRANS_RX_MAX) ?	\
				 TRANS_TX_MAX : TRANS_RX_MAX)

/*
 * In tx phase, the host prepares all the phy layer bytes of a request in the
 * phy buffer and sends them in a batch.
 *
 * The packet layer and physical layer defines several special chars for
 * various purpose, when a transaction layer byte hits one of these special
 * chars, it should be escaped. The escape rule is, "Escape char first,
 * following the byte XOR'ed with 0x20".
 *
 * This macro defines the max possible length of the phy data. In the worst
 * case, all transaction layer bytes need to be escaped (so the data length
 * doubles), plus 4 special chars (SOP, CHANNEL, CHANNEL_NUM, EOP). Finally
 * we should make sure the length is aligned to SPI BPW.
 */
#define PHY_TX_MAX		ALIGN(2 * TRANS_TX_MAX + 4, 4)

/*
 * Unlike tx, phy rx is affected by possible PHY_IDLE bytes from slave, the max
 * length of the rx bit stream is unpredictable. So the driver reads the words
 * one by one, and parses each word immediately into transaction layer buffer.
 * Only one word length of phy buffer is used for rx.
 */
#define PHY_BUF_SIZE		PHY_TX_MAX

/**
 * struct spi_avmm_bridge - SPI slave to AVMM bus master bridge
 *
 * @spi: spi slave associated with this bridge.
 * @word_len: bytes of word for spi transfer.
 * @trans_len: length of valid data in trans_buf.
 * @phy_len: length of valid data in phy_buf.
 * @trans_buf: the bridge buffer for transaction layer data.
 * @phy_buf: the bridge buffer for physical layer data.
 * @swap_words: the word swapping cb for phy data. NULL if not needed.
 *
 * As a device's registers are implemented on the AVMM bus address space, it
 * requires the driver to issue formatted requests to spi slave to AVMM bus
 * master bridge to perform register access.
 */
struct spi_avmm_bridge {
	struct spi_device *spi;
	unsigned char word_len;
	unsigned int trans_len;
	unsigned int phy_len;
	/* bridge buffer used in translation between protocol layers */
	char trans_buf[TRANS_BUF_SIZE];
	char phy_buf[PHY_BUF_SIZE];
	void (*swap_words)(void *buf, unsigned int len);
};

static void br_swap_words_32(void *buf, unsigned int len)
{
	swab32_array(buf, len / 4);
}

/*
 * Format transaction layer data in br->trans_buf according to the register
 * access request, Store valid transaction layer data length in br->trans_len.
 */
static int br_trans_tx_prepare(struct spi_avmm_bridge *br, bool is_read, u32 reg,
			       u32 *wr_val, u32 count)
{
	struct trans_req_header *header;
	unsigned int trans_len;
	u8 code;
	__le32 *data;
	int i;

	if (is_read) {
		if (count == 1)
			code = TRANS_CODE_READ;
		else
			code = TRANS_CODE_SEQ_READ;
	} else {
		if (count == 1)
			code = TRANS_CODE_WRITE;
		else
			code = TRANS_CODE_SEQ_WRITE;
	}

	header = (struct trans_req_header *)br->trans_buf;
	header->code = code;
	header->rsvd = 0;
	header->size = cpu_to_be16((u16)count * SPI_AVMM_VAL_SIZE);
	header->addr = cpu_to_be32(reg);

	trans_len = TRANS_REQ_HD_SIZE;

	if (!is_read) {
		trans_len += SPI_AVMM_VAL_SIZE * count;
		if (trans_len > sizeof(br->trans_buf))
			return -ENOMEM;

		data = (__le32 *)(br->trans_buf + TRANS_REQ_HD_SIZE);

		for (i = 0; i < count; i++)
			*data++ = cpu_to_le32(*wr_val++);
	}

	/* Store valid trans data length for next layer */
	br->trans_len = trans_len;

	return 0;
}

/*
 * Convert transaction layer data (in br->trans_buf) to phy layer data, store
 * them in br->phy_buf. Pad the phy_buf aligned with SPI's BPW. Store valid phy
 * layer data length in br->phy_len.
 *
 * phy_buf len should be aligned with SPI's BPW. Spare bytes should be padded
 * with PHY_IDLE, then the slave will just drop them.
 *
 * The driver will not simply pad 4a at the tail. The concern is that driver
 * will not store MISO data during tx phase, if the driver pads 4a at the tail,
 * it is possible that if the slave is fast enough to response at the padding
 * time. As a result these rx bytes are lost. In the following case, 7a,7c,00
 * will lost.
 * MOSI ...|7a|7c|00|10| |00|00|04|02| |4b|7d|5a|7b| |40|4a|4a|4a| |XX|XX|...
 * MISO ...|4a|4a|4a|4a| |4a|4a|4a|4a| |4a|4a|4a|4a| |4a|7a|7c|00| |78|56|...
 *
 * So the driver moves EOP and bytes after EOP to the end of the aligned size,
 * then fill the hole with PHY_IDLE. As following:
 * before pad ...|7a|7c|00|10| |00|00|04|02| |4b|7d|5a|7b| |40|
 * after pad  ...|7a|7c|00|10| |00|00|04|02| |4b|7d|5a|4a| |4a|4a|7b|40|
 * Then if the slave will not get the entire packet before the tx phase is
 * over, it can't responsed to anything either.
 */
static int br_pkt_phy_tx_prepare(struct spi_avmm_bridge *br)
{
	char *tb, *tb_end, *pb, *pb_limit, *pb_eop = NULL;
	unsigned int aligned_phy_len, move_size;
	bool need_esc = false;

	tb = br->trans_buf;
	tb_end = tb + br->trans_len;
	pb = br->phy_buf;
	pb_limit = pb + ARRAY_SIZE(br->phy_buf);

	*pb++ = PKT_SOP;

	/*
	 * The driver doesn't support multiple channels so the channel number
	 * is always 0.
	 */
	*pb++ = PKT_CHANNEL;
	*pb++ = 0x0;

	for (; pb < pb_limit && tb < tb_end; pb++) {
		if (need_esc) {
			*pb = *tb++ ^ 0x20;
			need_esc = false;
			continue;
		}

		/* EOP should be inserted before the last valid char */
		if (tb == tb_end - 1 && !pb_eop) {
			*pb = PKT_EOP;
			pb_eop = pb;
			continue;
		}

		/*
		 * insert an ESCAPE char if the data value equals any special
		 * char.
		 */
		switch (*tb) {
		case PKT_SOP:
		case PKT_EOP:
		case PKT_CHANNEL:
		case PKT_ESC:
			*pb = PKT_ESC;
			need_esc = true;
			break;
		case PHY_IDLE:
		case PHY_ESC:
			*pb = PHY_ESC;
			need_esc = true;
			break;
		default:
			*pb = *tb++;
			break;
		}
	}

	/* The phy buffer is used out but transaction layer data remains */
	if (tb < tb_end)
		return -ENOMEM;

	/* Store valid phy data length for spi transfer */
	br->phy_len = pb - br->phy_buf;

	if (br->word_len == 1)
		return 0;

	/* Do phy buf padding if word_len > 1 byte. */
	aligned_phy_len = ALIGN(br->phy_len, br->word_len);
	if (aligned_phy_len > sizeof(br->phy_buf))
		return -ENOMEM;

	if (aligned_phy_len == br->phy_len)
		return 0;

	/* move EOP and bytes after EOP to the end of aligned size */
	move_size = pb - pb_eop;
	memmove(&br->phy_buf[aligned_phy_len - move_size], pb_eop, move_size);

	/* fill the hole with PHY_IDLEs */
	memset(pb_eop, PHY_IDLE, aligned_phy_len - br->phy_len);

	/* update the phy data length */
	br->phy_len = aligned_phy_len;

	return 0;
}

/*
 * In tx phase, the slave only returns PHY_IDLE (0x4a). So the driver will
 * ignore rx in tx phase.
 */
static int br_do_tx(struct spi_avmm_bridge *br)
{
	/* reorder words for spi transfer */
	if (br->swap_words)
		br->swap_words(br->phy_buf, br->phy_len);

	/* send all data in phy_buf  */
	return spi_write(br->spi, br->phy_buf, br->phy_len);
}

/*
 * This function read the rx byte stream from SPI word by word and convert
 * them to transaction layer data in br->trans_buf. It also stores the length
 * of rx transaction layer data in br->trans_len
 *
 * The slave may send an unknown number of PHY_IDLEs in rx phase, so we cannot
 * prepare a fixed length buffer to receive all of the rx data in a batch. We
 * have to read word by word and convert them to transaction layer data at
 * once.
 */
static int br_do_rx_and_pkt_phy_parse(struct spi_avmm_bridge *br)
{
	bool eop_found = false, channel_found = false, esc_found = false;
	bool valid_word = false, last_try = false;
	struct device *dev = &br->spi->dev;
	char *pb, *tb_limit, *tb = NULL;
	unsigned long poll_timeout;
	int ret, i;

	tb_limit = br->trans_buf + ARRAY_SIZE(br->trans_buf);
	pb = br->phy_buf;
	poll_timeout = jiffies + SPI_AVMM_XFER_TIMEOUT;
	while (tb < tb_limit) {
		ret = spi_read(br->spi, pb, br->word_len);
		if (ret)
			return ret;

		/* reorder the word back */
		if (br->swap_words)
			br->swap_words(pb, br->word_len);

		valid_word = false;
		for (i = 0; i < br->word_len; i++) {
			/* drop everything before first SOP */
			if (!tb && pb[i] != PKT_SOP)
				continue;

			/* drop PHY_IDLE */
			if (pb[i] == PHY_IDLE)
				continue;

			valid_word = true;

			/*
			 * We don't support multiple channels, so error out if
			 * a non-zero channel number is found.
			 */
			if (channel_found) {
				if (pb[i] != 0) {
					dev_err(dev, "%s channel num != 0\n",
						__func__);
					return -EFAULT;
				}

				channel_found = false;
				continue;
			}

			switch (pb[i]) {
			case PKT_SOP:
				/*
				 * reset the parsing if a second SOP appears.
				 */
				tb = br->trans_buf;
				eop_found = false;
				channel_found = false;
				esc_found = false;
				break;
			case PKT_EOP:
				/*
				 * No special char is expected after ESC char.
				 * No special char (except ESC & PHY_IDLE) is
				 * expected after EOP char.
				 *
				 * The special chars are all dropped.
				 */
				if (esc_found || eop_found)
					return -EFAULT;

				eop_found = true;
				break;
			case PKT_CHANNEL:
				if (esc_found || eop_found)
					return -EFAULT;

				channel_found = true;
				break;
			case PKT_ESC:
			case PHY_ESC:
				if (esc_found)
					return -EFAULT;

				esc_found = true;
				break;
			default:
				/* Record the normal byte in trans_buf. */
				if (esc_found) {
					*tb++ = pb[i] ^ 0x20;
					esc_found = false;
				} else {
					*tb++ = pb[i];
				}

				/*
				 * We get the last normal byte after EOP, it is
				 * time we finish. Normally the function should
				 * return here.
				 */
				if (eop_found) {
					br->trans_len = tb - br->trans_buf;
					return 0;
				}
			}
		}

		if (valid_word) {
			/* update poll timeout when we get valid word */
			poll_timeout = jiffies + SPI_AVMM_XFER_TIMEOUT;
			last_try = false;
		} else {
			/*
			 * We timeout when rx keeps invalid for some time. But
			 * it is possible we are scheduled out for long time
			 * after a spi_read. So when we are scheduled in, a SW
			 * timeout happens. But actually HW may have worked fine and
			 * has been ready long time ago. So we need to do an extra
			 * read, if we get a valid word then we could continue rx,
			 * otherwise real a HW issue happens.
			 */
			if (last_try)
				return -ETIMEDOUT;

			if (time_after(jiffies, poll_timeout))
				last_try = true;
		}
	}

	/*
	 * We have used out all transfer layer buffer but cannot find the end
	 * of the byte stream.
	 */
	dev_err(dev, "%s transfer buffer is full but rx doesn't end\n",
		__func__);

	return -EFAULT;
}

/*
 * For read transactions, the avmm bus will directly return register values
 * without transaction response header.
 */
static int br_rd_trans_rx_parse(struct spi_avmm_bridge *br,
				u32 *val, unsigned int expected_count)
{
	unsigned int i, trans_len = br->trans_len;
	__le32 *data;

	if (expected_count * SPI_AVMM_VAL_SIZE != trans_len)
		return -EFAULT;

	data = (__le32 *)br->trans_buf;
	for (i = 0; i < expected_count; i++)
		*val++ = le32_to_cpu(*data++);

	return 0;
}

/*
 * For write transactions, the slave will return a transaction response
 * header.
 */
static int br_wr_trans_rx_parse(struct spi_avmm_bridge *br,
				unsigned int expected_count)
{
	unsigned int trans_len = br->trans_len;
	struct trans_resp_header *resp;
	u8 code;
	u16 val_len;

	if (trans_len != TRANS_RESP_HD_SIZE)
		return -EFAULT;

	resp = (struct trans_resp_header *)br->trans_buf;

	code = resp->r_code ^ 0x80;
	val_len = be16_to_cpu(resp->size);
	if (!val_len || val_len != expected_count * SPI_AVMM_VAL_SIZE)
		return -EFAULT;

	/* error out if the trans code doesn't align with the val size */
	if ((val_len == SPI_AVMM_VAL_SIZE && code != TRANS_CODE_WRITE) ||
	    (val_len > SPI_AVMM_VAL_SIZE && code != TRANS_CODE_SEQ_WRITE))
		return -EFAULT;

	return 0;
}

static int do_reg_access(void *context, bool is_read, unsigned int reg,
			 unsigned int *value, unsigned int count)
{
	struct spi_avmm_bridge *br = context;
	int ret;

	/* invalidate bridge buffers first */
	br->trans_len = 0;
	br->phy_len = 0;

	ret = br_trans_tx_prepare(br, is_read, reg, value, count);
	if (ret)
		return ret;

	ret = br_pkt_phy_tx_prepare(br);
	if (ret)
		return ret;

	ret = br_do_tx(br);
	if (ret)
		return ret;

	ret = br_do_rx_and_pkt_phy_parse(br);
	if (ret)
		return ret;

	if (is_read)
		return br_rd_trans_rx_parse(br, value, count);
	else
		return br_wr_trans_rx_parse(br, count);
}

static int regmap_spi_avmm_gather_write(void *context,
					const void *reg_buf, size_t reg_len,
					const void *val_buf, size_t val_len)
{
	if (reg_len != SPI_AVMM_REG_SIZE)
		return -EINVAL;

	if (!IS_ALIGNED(val_len, SPI_AVMM_VAL_SIZE))
		return -EINVAL;

	return do_reg_access(context, false, *(u32 *)reg_buf, (u32 *)val_buf,
			     val_len / SPI_AVMM_VAL_SIZE);
}

static int regmap_spi_avmm_write(void *context, const void *data, size_t bytes)
{
	if (bytes < SPI_AVMM_REG_SIZE + SPI_AVMM_VAL_SIZE)
		return -EINVAL;

	return regmap_spi_avmm_gather_write(context, data, SPI_AVMM_REG_SIZE,
					    data + SPI_AVMM_REG_SIZE,
					    bytes - SPI_AVMM_REG_SIZE);
}

static int regmap_spi_avmm_read(void *context,
				const void *reg_buf, size_t reg_len,
				void *val_buf, size_t val_len)
{
	if (reg_len != SPI_AVMM_REG_SIZE)
		return -EINVAL;

	if (!IS_ALIGNED(val_len, SPI_AVMM_VAL_SIZE))
		return -EINVAL;

	return do_reg_access(context, true, *(u32 *)reg_buf, val_buf,
			     (val_len / SPI_AVMM_VAL_SIZE));
}

static struct spi_avmm_bridge *
spi_avmm_bridge_ctx_gen(struct spi_device *spi)
{
	struct spi_avmm_bridge *br;

	if (!spi)
		return ERR_PTR(-ENODEV);

	/* Only support BPW == 8 or 32 now. Try 32 BPW first. */
	spi->mode = SPI_MODE_1;
	spi->bits_per_word = 32;
	if (spi_setup(spi)) {
		spi->bits_per_word = 8;
		if (spi_setup(spi))
			return ERR_PTR(-EINVAL);
	}

	br = kzalloc(sizeof(*br), GFP_KERNEL);
	if (!br)
		return ERR_PTR(-ENOMEM);

	br->spi = spi;
	br->word_len = spi->bits_per_word / 8;
	if (br->word_len == 4) {
		/*
		 * The protocol requires little endian byte order but MSB
		 * first. So driver needs to swap the byte order word by word
		 * if word length > 1.
		 */
		br->swap_words = br_swap_words_32;
	}

	return br;
}

static void spi_avmm_bridge_ctx_free(void *context)
{
	kfree(context);
}

static const struct regmap_bus regmap_spi_avmm_bus = {
	.write = regmap_spi_avmm_write,
	.gather_write = regmap_spi_avmm_gather_write,
	.read = regmap_spi_avmm_read,
	.reg_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.max_raw_read = SPI_AVMM_VAL_SIZE * MAX_READ_CNT,
	.max_raw_write = SPI_AVMM_VAL_SIZE * MAX_WRITE_CNT,
	.free_context = spi_avmm_bridge_ctx_free,
};

struct regmap *__regmap_init_spi_avmm(struct spi_device *spi,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name)
{
	struct spi_avmm_bridge *bridge;
	struct regmap *map;

	bridge = spi_avmm_bridge_ctx_gen(spi);
	if (IS_ERR(bridge))
		return ERR_CAST(bridge);

	map = __regmap_init(&spi->dev, &regmap_spi_avmm_bus,
			    bridge, config, lock_key, lock_name);
	if (IS_ERR(map)) {
		spi_avmm_bridge_ctx_free(bridge);
		return ERR_CAST(map);
	}

	return map;
}
EXPORT_SYMBOL_GPL(__regmap_init_spi_avmm);

struct regmap *__devm_regmap_init_spi_avmm(struct spi_device *spi,
					   const struct regmap_config *config,
					   struct lock_class_key *lock_key,
					   const char *lock_name)
{
	struct spi_avmm_bridge *bridge;
	struct regmap *map;

	bridge = spi_avmm_bridge_ctx_gen(spi);
	if (IS_ERR(bridge))
		return ERR_CAST(bridge);

	map = __devm_regmap_init(&spi->dev, &regmap_spi_avmm_bus,
				 bridge, config, lock_key, lock_name);
	if (IS_ERR(map)) {
		spi_avmm_bridge_ctx_free(bridge);
		return ERR_CAST(map);
	}

	return map;
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_spi_avmm);

MODULE_LICENSE("GPL v2");
