/*
 * Copyright (c) 2014-2015 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Algorithmic part of the firmware download.
 * To be included in the container file providing framework
 */

#define wil_err_fw(wil, fmt, arg...) wil_err(wil, "ERR[ FW ]" fmt, ##arg)
#define wil_dbg_fw(wil, fmt, arg...) wil_dbg(wil, "DBG[ FW ]" fmt, ##arg)
#define wil_hex_dump_fw(prefix_str, prefix_type, rowsize,		\
			groupsize, buf, len, ascii)			\
			print_hex_dump_debug("DBG[ FW ]" prefix_str,	\
					     prefix_type, rowsize,	\
					     groupsize, buf, len, ascii)

#define FW_ADDR_CHECK(ioaddr, val, msg) do { \
		ioaddr = wmi_buffer(wil, val); \
		if (!ioaddr) { \
			wil_err_fw(wil, "bad " msg ": 0x%08x\n", \
				   le32_to_cpu(val)); \
			return -EINVAL; \
		} \
	} while (0)

/**
 * wil_fw_verify - verify firmware file validity
 *
 * perform various checks for the firmware file header.
 * records are not validated.
 *
 * Return file size or negative error
 */
static int wil_fw_verify(struct wil6210_priv *wil, const u8 *data, size_t size)
{
	const struct wil_fw_record_head *hdr = (const void *)data;
	struct wil_fw_record_file_header fh;
	const struct wil_fw_record_file_header *fh_;
	u32 crc;
	u32 dlen;

	if (size % 4) {
		wil_err_fw(wil, "image size not aligned: %zu\n", size);
		return -EINVAL;
	}
	/* have enough data for the file header? */
	if (size < sizeof(*hdr) + sizeof(fh)) {
		wil_err_fw(wil, "file too short: %zu bytes\n", size);
		return -EINVAL;
	}

	/* start with the file header? */
	if (le16_to_cpu(hdr->type) != wil_fw_type_file_header) {
		wil_err_fw(wil, "no file header\n");
		return -EINVAL;
	}

	/* data_len */
	fh_ = (struct wil_fw_record_file_header *)&hdr[1];
	dlen = le32_to_cpu(fh_->data_len);
	if (dlen % 4) {
		wil_err_fw(wil, "data length not aligned: %lu\n", (ulong)dlen);
		return -EINVAL;
	}
	if (size < dlen) {
		wil_err_fw(wil, "file truncated at %zu/%lu\n",
			   size, (ulong)dlen);
		return -EINVAL;
	}
	if (dlen < sizeof(*hdr) + sizeof(fh)) {
		wil_err_fw(wil, "data length too short: %lu\n", (ulong)dlen);
		return -EINVAL;
	}

	/* signature */
	if (le32_to_cpu(fh_->signature) != WIL_FW_SIGNATURE) {
		wil_err_fw(wil, "bad header signature: 0x%08x\n",
			   le32_to_cpu(fh_->signature));
		return -EINVAL;
	}

	/* version */
	if (le32_to_cpu(fh_->version) > WIL_FW_FMT_VERSION) {
		wil_err_fw(wil, "unsupported header version: %d\n",
			   le32_to_cpu(fh_->version));
		return -EINVAL;
	}

	/* checksum. ~crc32(~0, data, size) when fh.crc set to 0*/
	fh = *fh_;
	fh.crc = 0;

	crc = crc32_le(~0, (unsigned char const *)hdr, sizeof(*hdr));
	crc = crc32_le(crc, (unsigned char const *)&fh, sizeof(fh));
	crc = crc32_le(crc, (unsigned char const *)&fh_[1],
		       dlen - sizeof(*hdr) - sizeof(fh));
	crc = ~crc;

	if (crc != le32_to_cpu(fh_->crc)) {
		wil_err_fw(wil, "checksum mismatch:"
			   " calculated for %lu bytes 0x%08x != 0x%08x\n",
			   (ulong)dlen, crc, le32_to_cpu(fh_->crc));
		return -EINVAL;
	}

	return (int)dlen;
}

static int fw_handle_comment(struct wil6210_priv *wil, const void *data,
			     size_t size)
{
	wil_hex_dump_fw("", DUMP_PREFIX_OFFSET, 16, 1, data, size, true);

	return 0;
}

static int fw_handle_data(struct wil6210_priv *wil, const void *data,
			  size_t size)
{
	const struct wil_fw_record_data *d = data;
	void __iomem *dst;
	size_t s = size - sizeof(*d);

	if (size < sizeof(*d) + sizeof(u32)) {
		wil_err_fw(wil, "data record too short: %zu\n", size);
		return -EINVAL;
	}

	FW_ADDR_CHECK(dst, d->addr, "address");
	wil_dbg_fw(wil, "write [0x%08x] <== %zu bytes\n", le32_to_cpu(d->addr),
		   s);
	wil_memcpy_toio_32(dst, d->data, s);
	wmb(); /* finish before processing next record */

	return 0;
}

static int fw_handle_fill(struct wil6210_priv *wil, const void *data,
			  size_t size)
{
	const struct wil_fw_record_fill *d = data;
	void __iomem *dst;
	u32 v;
	size_t s = (size_t)le32_to_cpu(d->size);

	if (size != sizeof(*d)) {
		wil_err_fw(wil, "bad size for fill record: %zu\n", size);
		return -EINVAL;
	}

	if (s < sizeof(u32)) {
		wil_err_fw(wil, "fill size too short: %zu\n", s);
		return -EINVAL;
	}

	if (s % sizeof(u32)) {
		wil_err_fw(wil, "fill size not aligned: %zu\n", s);
		return -EINVAL;
	}

	FW_ADDR_CHECK(dst, d->addr, "address");

	v = le32_to_cpu(d->value);
	wil_dbg_fw(wil, "fill [0x%08x] <== 0x%08x, %zu bytes\n",
		   le32_to_cpu(d->addr), v, s);
	wil_memset_toio_32(dst, v, s);
	wmb(); /* finish before processing next record */

	return 0;
}

static int fw_handle_file_header(struct wil6210_priv *wil, const void *data,
				 size_t size)
{
	const struct wil_fw_record_file_header *d = data;

	if (size != sizeof(*d)) {
		wil_err_fw(wil, "file header length incorrect: %zu\n", size);
		return -EINVAL;
	}

	wil_dbg_fw(wil, "new file, ver. %d, %i bytes\n",
		   d->version, d->data_len);
	wil_hex_dump_fw("", DUMP_PREFIX_OFFSET, 16, 1, d->comment,
			sizeof(d->comment), true);

	return 0;
}

static int fw_handle_direct_write(struct wil6210_priv *wil, const void *data,
				  size_t size)
{
	const struct wil_fw_record_direct_write *d = data;
	const struct wil_fw_data_dwrite *block = d->data;
	int n, i;

	if (size % sizeof(*block)) {
		wil_err_fw(wil, "record size not aligned on %zu: %zu\n",
			   sizeof(*block), size);
		return -EINVAL;
	}
	n = size / sizeof(*block);

	for (i = 0; i < n; i++) {
		void __iomem *dst;
		u32 m = le32_to_cpu(block[i].mask);
		u32 v = le32_to_cpu(block[i].value);
		u32 x, y;

		FW_ADDR_CHECK(dst, block[i].addr, "address");

		x = readl(dst);
		y = (x & m) | (v & ~m);
		wil_dbg_fw(wil, "write [0x%08x] <== 0x%08x "
			   "(old 0x%08x val 0x%08x mask 0x%08x)\n",
			   le32_to_cpu(block[i].addr), y, x, v, m);
		writel(y, dst);
		wmb(); /* finish before processing next record */
	}

	return 0;
}

static int gw_write(struct wil6210_priv *wil, void __iomem *gwa_addr,
		    void __iomem *gwa_cmd, void __iomem *gwa_ctl, u32 gw_cmd,
		    u32 a)
{
	unsigned delay = 0;

	writel(a, gwa_addr);
	writel(gw_cmd, gwa_cmd);
	wmb(); /* finish before activate gw */

	writel(WIL_FW_GW_CTL_RUN, gwa_ctl); /* activate gw */
	do {
		udelay(1); /* typical time is few usec */
		if (delay++ > 100) {
			wil_err_fw(wil, "gw timeout\n");
			return -EINVAL;
		}
	} while (readl(gwa_ctl) & WIL_FW_GW_CTL_BUSY); /* gw done? */

	return 0;
}

static int fw_handle_gateway_data(struct wil6210_priv *wil, const void *data,
				  size_t size)
{
	const struct wil_fw_record_gateway_data *d = data;
	const struct wil_fw_data_gw *block = d->data;
	void __iomem *gwa_addr;
	void __iomem *gwa_val;
	void __iomem *gwa_cmd;
	void __iomem *gwa_ctl;
	u32 gw_cmd;
	int n, i;

	if (size < sizeof(*d) + sizeof(*block)) {
		wil_err_fw(wil, "gateway record too short: %zu\n", size);
		return -EINVAL;
	}

	if ((size - sizeof(*d)) % sizeof(*block)) {
		wil_err_fw(wil, "gateway record data size"
			   " not aligned on %zu: %zu\n",
			   sizeof(*block), size - sizeof(*d));
		return -EINVAL;
	}
	n = (size - sizeof(*d)) / sizeof(*block);

	gw_cmd = le32_to_cpu(d->command);

	wil_dbg_fw(wil, "gw write record [%3d] blocks, cmd 0x%08x\n",
		   n, gw_cmd);

	FW_ADDR_CHECK(gwa_addr, d->gateway_addr_addr, "gateway_addr_addr");
	FW_ADDR_CHECK(gwa_val, d->gateway_value_addr, "gateway_value_addr");
	FW_ADDR_CHECK(gwa_cmd, d->gateway_cmd_addr, "gateway_cmd_addr");
	FW_ADDR_CHECK(gwa_ctl, d->gateway_ctrl_address, "gateway_ctrl_address");

	wil_dbg_fw(wil, "gw addresses: addr 0x%08x val 0x%08x"
		   " cmd 0x%08x ctl 0x%08x\n",
		   le32_to_cpu(d->gateway_addr_addr),
		   le32_to_cpu(d->gateway_value_addr),
		   le32_to_cpu(d->gateway_cmd_addr),
		   le32_to_cpu(d->gateway_ctrl_address));

	for (i = 0; i < n; i++) {
		int rc;
		u32 a = le32_to_cpu(block[i].addr);
		u32 v = le32_to_cpu(block[i].value);

		wil_dbg_fw(wil, "  gw write[%3d] [0x%08x] <== 0x%08x\n",
			   i, a, v);

		writel(v, gwa_val);
		rc = gw_write(wil, gwa_addr, gwa_cmd, gwa_ctl, gw_cmd, a);
		if (rc)
			return rc;
	}

	return 0;
}

static int fw_handle_gateway_data4(struct wil6210_priv *wil, const void *data,
				   size_t size)
{
	const struct wil_fw_record_gateway_data4 *d = data;
	const struct wil_fw_data_gw4 *block = d->data;
	void __iomem *gwa_addr;
	void __iomem *gwa_val[ARRAY_SIZE(block->value)];
	void __iomem *gwa_cmd;
	void __iomem *gwa_ctl;
	u32 gw_cmd;
	int n, i, k;

	if (size < sizeof(*d) + sizeof(*block)) {
		wil_err_fw(wil, "gateway4 record too short: %zu\n", size);
		return -EINVAL;
	}

	if ((size - sizeof(*d)) % sizeof(*block)) {
		wil_err_fw(wil, "gateway4 record data size"
			   " not aligned on %zu: %zu\n",
			   sizeof(*block), size - sizeof(*d));
		return -EINVAL;
	}
	n = (size - sizeof(*d)) / sizeof(*block);

	gw_cmd = le32_to_cpu(d->command);

	wil_dbg_fw(wil, "gw4 write record [%3d] blocks, cmd 0x%08x\n",
		   n, gw_cmd);

	FW_ADDR_CHECK(gwa_addr, d->gateway_addr_addr, "gateway_addr_addr");
	for (k = 0; k < ARRAY_SIZE(block->value); k++)
		FW_ADDR_CHECK(gwa_val[k], d->gateway_value_addr[k],
			      "gateway_value_addr");
	FW_ADDR_CHECK(gwa_cmd, d->gateway_cmd_addr, "gateway_cmd_addr");
	FW_ADDR_CHECK(gwa_ctl, d->gateway_ctrl_address, "gateway_ctrl_address");

	wil_dbg_fw(wil, "gw4 addresses: addr 0x%08x cmd 0x%08x ctl 0x%08x\n",
		   le32_to_cpu(d->gateway_addr_addr),
		   le32_to_cpu(d->gateway_cmd_addr),
		   le32_to_cpu(d->gateway_ctrl_address));
	wil_hex_dump_fw("val addresses: ", DUMP_PREFIX_NONE, 16, 4,
			d->gateway_value_addr, sizeof(d->gateway_value_addr),
			false);

	for (i = 0; i < n; i++) {
		int rc;
		u32 a = le32_to_cpu(block[i].addr);
		u32 v[ARRAY_SIZE(block->value)];

		for (k = 0; k < ARRAY_SIZE(block->value); k++)
			v[k] = le32_to_cpu(block[i].value[k]);

		wil_dbg_fw(wil, "  gw4 write[%3d] [0x%08x] <==\n", i, a);
		wil_hex_dump_fw("    val ", DUMP_PREFIX_NONE, 16, 4, v,
				sizeof(v), false);

		for (k = 0; k < ARRAY_SIZE(block->value); k++)
			writel(v[k], gwa_val[k]);
		rc = gw_write(wil, gwa_addr, gwa_cmd, gwa_ctl, gw_cmd, a);
		if (rc)
			return rc;
	}

	return 0;
}

static const struct {
	int type;
	int (*handler)(struct wil6210_priv *wil, const void *data, size_t size);
} wil_fw_handlers[] = {
	{wil_fw_type_comment, fw_handle_comment},
	{wil_fw_type_data, fw_handle_data},
	{wil_fw_type_fill, fw_handle_fill},
	/* wil_fw_type_action */
	/* wil_fw_type_verify */
	{wil_fw_type_file_header, fw_handle_file_header},
	{wil_fw_type_direct_write, fw_handle_direct_write},
	{wil_fw_type_gateway_data, fw_handle_gateway_data},
	{wil_fw_type_gateway_data4, fw_handle_gateway_data4},
};

static int wil_fw_handle_record(struct wil6210_priv *wil, int type,
				const void *data, size_t size)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wil_fw_handlers); i++) {
		if (wil_fw_handlers[i].type == type)
			return wil_fw_handlers[i].handler(wil, data, size);
	}

	wil_err_fw(wil, "unknown record type: %d\n", type);
	return -EINVAL;
}

/**
 * wil_fw_load - load FW into device
 *
 * Load the FW and uCode code and data to the corresponding device
 * memory regions
 *
 * Return error code
 */
static int wil_fw_load(struct wil6210_priv *wil, const void *data, size_t size)
{
	int rc = 0;
	const struct wil_fw_record_head *hdr;
	size_t s, hdr_sz;

	for (hdr = data;; hdr = (const void *)hdr + s, size -= s) {
		if (size < sizeof(*hdr))
			break;
		hdr_sz = le32_to_cpu(hdr->size);
		s = sizeof(*hdr) + hdr_sz;
		if (s > size)
			break;
		if (hdr_sz % 4) {
			wil_err_fw(wil, "unaligned record size: %zu\n",
				   hdr_sz);
			return -EINVAL;
		}
		rc = wil_fw_handle_record(wil, le16_to_cpu(hdr->type),
					  &hdr[1], hdr_sz);
		if (rc)
			return rc;
	}
	if (size) {
		wil_err_fw(wil, "unprocessed bytes: %zu\n", size);
		if (size >= sizeof(*hdr)) {
			wil_err_fw(wil, "Stop at offset %ld"
				   " record type %d [%zd bytes]\n",
				   (long)((const void *)hdr - data),
				   le16_to_cpu(hdr->type), hdr_sz);
		}
		return -EINVAL;
	}

	return rc;
}

/**
 * wil_request_firmware - Request firmware and load to device
 *
 * Request firmware image from the file and load it to device
 *
 * Return error code
 */
int wil_request_firmware(struct wil6210_priv *wil, const char *name)
{
	int rc, rc1;
	const struct firmware *fw;
	size_t sz;
	const void *d;

	rc = request_firmware(&fw, name, wil_to_dev(wil));
	if (rc) {
		wil_err_fw(wil, "Failed to load firmware %s\n", name);
		return rc;
	}
	wil_dbg_fw(wil, "Loading <%s>, %zu bytes\n", name, fw->size);

	for (sz = fw->size, d = fw->data; sz; sz -= rc1, d += rc1) {
		rc1 = wil_fw_verify(wil, d, sz);
		if (rc1 < 0) {
			rc = rc1;
			goto out;
		}
		rc = wil_fw_load(wil, d, rc1);
		if (rc < 0)
			goto out;
	}

out:
	release_firmware(fw);
	return rc;
}
