// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2013 Broadcom Corporation
 */

#include <linux/efi.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/bcm47xx_nvram.h>

#include "debug.h"
#include "firmware.h"
#include "core.h"
#include "common.h"
#include "chip.h"

#define BRCMF_FW_MAX_NVRAM_SIZE			64000
#define BRCMF_FW_NVRAM_DEVPATH_LEN		19	/* devpath0=pcie/1/4/ */
#define BRCMF_FW_NVRAM_PCIEDEV_LEN		10	/* pcie/1/4/ + \0 */
#define BRCMF_FW_DEFAULT_BOARDREV		"boardrev=0xff"
#define BRCMF_FW_MACADDR_FMT			"macaddr=%pM"
#define BRCMF_FW_MACADDR_LEN			(7 + ETH_ALEN * 3)

enum nvram_parser_state {
	IDLE,
	KEY,
	VALUE,
	COMMENT,
	END
};

/**
 * struct nvram_parser - internal info for parser.
 *
 * @state: current parser state.
 * @data: input buffer being parsed.
 * @nvram: output buffer with parse result.
 * @nvram_len: length of parse result.
 * @line: current line.
 * @column: current column in line.
 * @pos: byte offset in input buffer.
 * @entry: start position of key,value entry.
 * @multi_dev_v1: detect pcie multi device v1 (compressed).
 * @multi_dev_v2: detect pcie multi device v2.
 * @boardrev_found: nvram contains boardrev information.
 * @strip_mac: strip the MAC address.
 */
struct nvram_parser {
	enum nvram_parser_state state;
	const u8 *data;
	u8 *nvram;
	u32 nvram_len;
	u32 line;
	u32 column;
	u32 pos;
	u32 entry;
	bool multi_dev_v1;
	bool multi_dev_v2;
	bool boardrev_found;
	bool strip_mac;
};

/*
 * is_nvram_char() - check if char is a valid one for NVRAM entry
 *
 * It accepts all printable ASCII chars except for '#' which opens a comment.
 * Please note that ' ' (space) while accepted is not a valid key name char.
 */
static bool is_nvram_char(char c)
{
	/* comment marker excluded */
	if (c == '#')
		return false;

	/* key and value may have any other readable character */
	return (c >= 0x20 && c < 0x7f);
}

static bool is_whitespace(char c)
{
	return (c == ' ' || c == '\r' || c == '\n' || c == '\t');
}

static enum nvram_parser_state brcmf_nvram_handle_idle(struct nvram_parser *nvp)
{
	char c;

	c = nvp->data[nvp->pos];
	if (c == '\n')
		return COMMENT;
	if (is_whitespace(c) || c == '\0')
		goto proceed;
	if (c == '#')
		return COMMENT;
	if (is_nvram_char(c)) {
		nvp->entry = nvp->pos;
		return KEY;
	}
	brcmf_dbg(INFO, "warning: ln=%d:col=%d: ignoring invalid character\n",
		  nvp->line, nvp->column);
proceed:
	nvp->column++;
	nvp->pos++;
	return IDLE;
}

static enum nvram_parser_state brcmf_nvram_handle_key(struct nvram_parser *nvp)
{
	enum nvram_parser_state st = nvp->state;
	char c;

	c = nvp->data[nvp->pos];
	if (c == '=') {
		/* ignore RAW1 by treating as comment */
		if (strncmp(&nvp->data[nvp->entry], "RAW1", 4) == 0)
			st = COMMENT;
		else
			st = VALUE;
		if (strncmp(&nvp->data[nvp->entry], "devpath", 7) == 0)
			nvp->multi_dev_v1 = true;
		if (strncmp(&nvp->data[nvp->entry], "pcie/", 5) == 0)
			nvp->multi_dev_v2 = true;
		if (strncmp(&nvp->data[nvp->entry], "boardrev", 8) == 0)
			nvp->boardrev_found = true;
		/* strip macaddr if platform MAC overrides */
		if (nvp->strip_mac &&
		    strncmp(&nvp->data[nvp->entry], "macaddr", 7) == 0)
			st = COMMENT;
	} else if (!is_nvram_char(c) || c == ' ') {
		brcmf_dbg(INFO, "warning: ln=%d:col=%d: '=' expected, skip invalid key entry\n",
			  nvp->line, nvp->column);
		return COMMENT;
	}

	nvp->column++;
	nvp->pos++;
	return st;
}

static enum nvram_parser_state
brcmf_nvram_handle_value(struct nvram_parser *nvp)
{
	char c;
	char *skv;
	char *ekv;
	u32 cplen;

	c = nvp->data[nvp->pos];
	if (!is_nvram_char(c)) {
		/* key,value pair complete */
		ekv = (u8 *)&nvp->data[nvp->pos];
		skv = (u8 *)&nvp->data[nvp->entry];
		cplen = ekv - skv;
		if (nvp->nvram_len + cplen + 1 >= BRCMF_FW_MAX_NVRAM_SIZE)
			return END;
		/* copy to output buffer */
		memcpy(&nvp->nvram[nvp->nvram_len], skv, cplen);
		nvp->nvram_len += cplen;
		nvp->nvram[nvp->nvram_len] = '\0';
		nvp->nvram_len++;
		return IDLE;
	}
	nvp->pos++;
	nvp->column++;
	return VALUE;
}

static enum nvram_parser_state
brcmf_nvram_handle_comment(struct nvram_parser *nvp)
{
	char *eoc, *sol;

	sol = (char *)&nvp->data[nvp->pos];
	eoc = strchr(sol, '\n');
	if (!eoc) {
		eoc = strchr(sol, '\0');
		if (!eoc)
			return END;
	}

	/* eat all moving to next line */
	nvp->line++;
	nvp->column = 1;
	nvp->pos += (eoc - sol) + 1;
	return IDLE;
}

static enum nvram_parser_state brcmf_nvram_handle_end(struct nvram_parser *nvp)
{
	/* final state */
	return END;
}

static enum nvram_parser_state
(*nv_parser_states[])(struct nvram_parser *nvp) = {
	brcmf_nvram_handle_idle,
	brcmf_nvram_handle_key,
	brcmf_nvram_handle_value,
	brcmf_nvram_handle_comment,
	brcmf_nvram_handle_end
};

static int brcmf_init_nvram_parser(struct nvram_parser *nvp,
				   const u8 *data, size_t data_len)
{
	size_t size;

	memset(nvp, 0, sizeof(*nvp));
	nvp->data = data;
	/* Limit size to MAX_NVRAM_SIZE, some files contain lot of comment */
	if (data_len > BRCMF_FW_MAX_NVRAM_SIZE)
		size = BRCMF_FW_MAX_NVRAM_SIZE;
	else
		size = data_len;
	/* Add space for properties we may add */
	size += strlen(BRCMF_FW_DEFAULT_BOARDREV) + 1;
	size += BRCMF_FW_MACADDR_LEN + 1;
	/* Alloc for extra 0 byte + roundup by 4 + length field */
	size += 1 + 3 + sizeof(u32);
	nvp->nvram = kzalloc(size, GFP_KERNEL);
	if (!nvp->nvram)
		return -ENOMEM;

	nvp->line = 1;
	nvp->column = 1;
	return 0;
}

/* brcmf_fw_strip_multi_v1 :Some nvram files contain settings for multiple
 * devices. Strip it down for one device, use domain_nr/bus_nr to determine
 * which data is to be returned. v1 is the version where nvram is stored
 * compressed and "devpath" maps to index for valid entries.
 */
static void brcmf_fw_strip_multi_v1(struct nvram_parser *nvp, u16 domain_nr,
				    u16 bus_nr)
{
	/* Device path with a leading '=' key-value separator */
	char pci_path[] = "=pci/?/?";
	size_t pci_len;
	char pcie_path[] = "=pcie/?/?";
	size_t pcie_len;

	u32 i, j;
	bool found;
	u8 *nvram;
	u8 id;

	nvram = kzalloc(nvp->nvram_len + 1 + 3 + sizeof(u32), GFP_KERNEL);
	if (!nvram)
		goto fail;

	/* min length: devpath0=pcie/1/4/ + 0:x=y */
	if (nvp->nvram_len < BRCMF_FW_NVRAM_DEVPATH_LEN + 6)
		goto fail;

	/* First search for the devpathX and see if it is the configuration
	 * for domain_nr/bus_nr. Search complete nvp
	 */
	snprintf(pci_path, sizeof(pci_path), "=pci/%d/%d", domain_nr,
		 bus_nr);
	pci_len = strlen(pci_path);
	snprintf(pcie_path, sizeof(pcie_path), "=pcie/%d/%d", domain_nr,
		 bus_nr);
	pcie_len = strlen(pcie_path);
	found = false;
	i = 0;
	while (i < nvp->nvram_len - BRCMF_FW_NVRAM_DEVPATH_LEN) {
		/* Format: devpathX=pcie/Y/Z/
		 * Y = domain_nr, Z = bus_nr, X = virtual ID
		 */
		if (strncmp(&nvp->nvram[i], "devpath", 7) == 0 &&
		    (!strncmp(&nvp->nvram[i + 8], pci_path, pci_len) ||
		     !strncmp(&nvp->nvram[i + 8], pcie_path, pcie_len))) {
			id = nvp->nvram[i + 7] - '0';
			found = true;
			break;
		}
		while (nvp->nvram[i] != 0)
			i++;
		i++;
	}
	if (!found)
		goto fail;

	/* Now copy all valid entries, release old nvram and assign new one */
	i = 0;
	j = 0;
	while (i < nvp->nvram_len) {
		if ((nvp->nvram[i] - '0' == id) && (nvp->nvram[i + 1] == ':')) {
			i += 2;
			if (strncmp(&nvp->nvram[i], "boardrev", 8) == 0)
				nvp->boardrev_found = true;
			while (nvp->nvram[i] != 0) {
				nvram[j] = nvp->nvram[i];
				i++;
				j++;
			}
			nvram[j] = 0;
			j++;
		}
		while (nvp->nvram[i] != 0)
			i++;
		i++;
	}
	kfree(nvp->nvram);
	nvp->nvram = nvram;
	nvp->nvram_len = j;
	return;

fail:
	kfree(nvram);
	nvp->nvram_len = 0;
}

/* brcmf_fw_strip_multi_v2 :Some nvram files contain settings for multiple
 * devices. Strip it down for one device, use domain_nr/bus_nr to determine
 * which data is to be returned. v2 is the version where nvram is stored
 * uncompressed, all relevant valid entries are identified by
 * pcie/domain_nr/bus_nr:
 */
static void brcmf_fw_strip_multi_v2(struct nvram_parser *nvp, u16 domain_nr,
				    u16 bus_nr)
{
	char prefix[BRCMF_FW_NVRAM_PCIEDEV_LEN];
	size_t len;
	u32 i, j;
	u8 *nvram;

	nvram = kzalloc(nvp->nvram_len + 1 + 3 + sizeof(u32), GFP_KERNEL);
	if (!nvram) {
		nvp->nvram_len = 0;
		return;
	}

	/* Copy all valid entries, release old nvram and assign new one.
	 * Valid entries are of type pcie/X/Y/ where X = domain_nr and
	 * Y = bus_nr.
	 */
	snprintf(prefix, sizeof(prefix), "pcie/%d/%d/", domain_nr, bus_nr);
	len = strlen(prefix);
	i = 0;
	j = 0;
	while (i < nvp->nvram_len - len) {
		if (strncmp(&nvp->nvram[i], prefix, len) == 0) {
			i += len;
			if (strncmp(&nvp->nvram[i], "boardrev", 8) == 0)
				nvp->boardrev_found = true;
			while (nvp->nvram[i] != 0) {
				nvram[j] = nvp->nvram[i];
				i++;
				j++;
			}
			nvram[j] = 0;
			j++;
		}
		while (nvp->nvram[i] != 0)
			i++;
		i++;
	}
	kfree(nvp->nvram);
	nvp->nvram = nvram;
	nvp->nvram_len = j;
}

static void brcmf_fw_add_defaults(struct nvram_parser *nvp)
{
	if (nvp->boardrev_found)
		return;

	memcpy(&nvp->nvram[nvp->nvram_len], &BRCMF_FW_DEFAULT_BOARDREV,
	       strlen(BRCMF_FW_DEFAULT_BOARDREV));
	nvp->nvram_len += strlen(BRCMF_FW_DEFAULT_BOARDREV);
	nvp->nvram[nvp->nvram_len] = '\0';
	nvp->nvram_len++;
}

static void brcmf_fw_add_macaddr(struct nvram_parser *nvp, u8 *mac)
{
	int len;

	len = scnprintf(&nvp->nvram[nvp->nvram_len], BRCMF_FW_MACADDR_LEN + 1,
			BRCMF_FW_MACADDR_FMT, mac);
	WARN_ON(len != BRCMF_FW_MACADDR_LEN);
	nvp->nvram_len += len + 1;
}

/* brcmf_nvram_strip :Takes a buffer of "<var>=<value>\n" lines read from a fil
 * and ending in a NUL. Removes carriage returns, empty lines, comment lines,
 * and converts newlines to NULs. Shortens buffer as needed and pads with NULs.
 * End of buffer is completed with token identifying length of buffer.
 */
static void *brcmf_fw_nvram_strip(const u8 *data, size_t data_len,
				  u32 *new_length, u16 domain_nr, u16 bus_nr,
				  struct device *dev)
{
	struct nvram_parser nvp;
	u32 pad;
	u32 token;
	__le32 token_le;
	u8 mac[ETH_ALEN];

	if (brcmf_init_nvram_parser(&nvp, data, data_len) < 0)
		return NULL;

	if (eth_platform_get_mac_address(dev, mac) == 0)
		nvp.strip_mac = true;

	while (nvp.pos < data_len) {
		nvp.state = nv_parser_states[nvp.state](&nvp);
		if (nvp.state == END)
			break;
	}
	if (nvp.multi_dev_v1) {
		nvp.boardrev_found = false;
		brcmf_fw_strip_multi_v1(&nvp, domain_nr, bus_nr);
	} else if (nvp.multi_dev_v2) {
		nvp.boardrev_found = false;
		brcmf_fw_strip_multi_v2(&nvp, domain_nr, bus_nr);
	}

	if (nvp.nvram_len == 0) {
		kfree(nvp.nvram);
		return NULL;
	}

	brcmf_fw_add_defaults(&nvp);

	if (nvp.strip_mac)
		brcmf_fw_add_macaddr(&nvp, mac);

	pad = nvp.nvram_len;
	*new_length = roundup(nvp.nvram_len + 1, 4);
	while (pad != *new_length) {
		nvp.nvram[pad] = 0;
		pad++;
	}

	token = *new_length / 4;
	token = (~token << 16) | (token & 0x0000FFFF);
	token_le = cpu_to_le32(token);

	memcpy(&nvp.nvram[*new_length], &token_le, sizeof(token_le));
	*new_length += sizeof(token_le);

	return nvp.nvram;
}

void brcmf_fw_nvram_free(void *nvram)
{
	kfree(nvram);
}

struct brcmf_fw {
	struct device *dev;
	struct brcmf_fw_request *req;
	u32 curpos;
	unsigned int board_index;
	void (*done)(struct device *dev, int err, struct brcmf_fw_request *req);
};

#ifdef CONFIG_EFI
/* In some cases the EFI-var stored nvram contains "ccode=ALL" or "ccode=XV"
 * to specify "worldwide" compatible settings, but these 2 ccode-s do not work
 * properly. "ccode=ALL" causes channels 12 and 13 to not be available,
 * "ccode=XV" causes all 5GHz channels to not be available. So we replace both
 * with "ccode=X2" which allows channels 12+13 and 5Ghz channels in
 * no-Initiate-Radiation mode. This means that we will never send on these
 * channels without first having received valid wifi traffic on the channel.
 */
static void brcmf_fw_fix_efi_nvram_ccode(char *data, unsigned long data_len)
{
	char *ccode;

	ccode = strnstr((char *)data, "ccode=ALL", data_len);
	if (!ccode)
		ccode = strnstr((char *)data, "ccode=XV\r", data_len);
	if (!ccode)
		return;

	ccode[6] = 'X';
	ccode[7] = '2';
	ccode[8] = '\r';
}

static u8 *brcmf_fw_nvram_from_efi(size_t *data_len_ret)
{
	efi_guid_t guid = EFI_GUID(0x74b00bd9, 0x805a, 0x4d61, 0xb5, 0x1f,
				   0x43, 0x26, 0x81, 0x23, 0xd1, 0x13);
	unsigned long data_len = 0;
	efi_status_t status;
	u8 *data = NULL;

	if (!efi_rt_services_supported(EFI_RT_SUPPORTED_GET_VARIABLE))
		return NULL;

	status = efi.get_variable(L"nvram", &guid, NULL, &data_len, NULL);
	if (status != EFI_BUFFER_TOO_SMALL)
		goto fail;

	data = kmalloc(data_len, GFP_KERNEL);
	if (!data)
		goto fail;

	status = efi.get_variable(L"nvram", &guid, NULL, &data_len, data);
	if (status != EFI_SUCCESS)
		goto fail;

	brcmf_fw_fix_efi_nvram_ccode(data, data_len);
	brcmf_info("Using nvram EFI variable\n");

	*data_len_ret = data_len;
	return data;
fail:
	kfree(data);
	return NULL;
}
#else
static inline u8 *brcmf_fw_nvram_from_efi(size_t *data_len) { return NULL; }
#endif

static void brcmf_fw_free_request(struct brcmf_fw_request *req)
{
	struct brcmf_fw_item *item;
	int i;

	for (i = 0, item = &req->items[0]; i < req->n_items; i++, item++) {
		if (item->type == BRCMF_FW_TYPE_BINARY)
			release_firmware(item->binary);
		else if (item->type == BRCMF_FW_TYPE_NVRAM)
			brcmf_fw_nvram_free(item->nv_data.data);
	}
	kfree(req);
}

static int brcmf_fw_request_nvram_done(const struct firmware *fw, void *ctx)
{
	struct brcmf_fw *fwctx = ctx;
	struct brcmf_fw_item *cur;
	bool free_bcm47xx_nvram = false;
	bool kfree_nvram = false;
	u32 nvram_length = 0;
	void *nvram = NULL;
	u8 *data = NULL;
	size_t data_len;

	brcmf_dbg(TRACE, "enter: dev=%s\n", dev_name(fwctx->dev));

	cur = &fwctx->req->items[fwctx->curpos];

	if (fw && fw->data) {
		data = (u8 *)fw->data;
		data_len = fw->size;
	} else {
		if ((data = bcm47xx_nvram_get_contents(&data_len)))
			free_bcm47xx_nvram = true;
		else if ((data = brcmf_fw_nvram_from_efi(&data_len)))
			kfree_nvram = true;
		else if (!(cur->flags & BRCMF_FW_REQF_OPTIONAL))
			goto fail;
	}

	if (data)
		nvram = brcmf_fw_nvram_strip(data, data_len, &nvram_length,
					     fwctx->req->domain_nr,
					     fwctx->req->bus_nr,
					     fwctx->dev);

	if (free_bcm47xx_nvram)
		bcm47xx_nvram_release_contents(data);
	if (kfree_nvram)
		kfree(data);

	release_firmware(fw);
	if (!nvram && !(cur->flags & BRCMF_FW_REQF_OPTIONAL))
		goto fail;

	brcmf_dbg(TRACE, "nvram %p len %d\n", nvram, nvram_length);
	cur->nv_data.data = nvram;
	cur->nv_data.len = nvram_length;
	return 0;

fail:
	return -ENOENT;
}

static int brcmf_fw_complete_request(const struct firmware *fw,
				     struct brcmf_fw *fwctx)
{
	struct brcmf_fw_item *cur = &fwctx->req->items[fwctx->curpos];
	int ret = 0;

	brcmf_dbg(TRACE, "firmware %s %sfound\n", cur->path, fw ? "" : "not ");

	switch (cur->type) {
	case BRCMF_FW_TYPE_NVRAM:
		ret = brcmf_fw_request_nvram_done(fw, fwctx);
		break;
	case BRCMF_FW_TYPE_BINARY:
		if (fw)
			cur->binary = fw;
		else
			ret = -ENOENT;
		break;
	default:
		/* something fishy here so bail out early */
		brcmf_err("unknown fw type: %d\n", cur->type);
		release_firmware(fw);
		ret = -EINVAL;
	}

	return (cur->flags & BRCMF_FW_REQF_OPTIONAL) ? 0 : ret;
}

static char *brcm_alt_fw_path(const char *path, const char *board_type)
{
	char base[BRCMF_FW_NAME_LEN];
	const char *suffix;
	char *ret;

	if (!board_type)
		return NULL;

	suffix = strrchr(path, '.');
	if (!suffix || suffix == path)
		return NULL;

	/* strip extension at the end */
	strscpy(base, path, BRCMF_FW_NAME_LEN);
	base[suffix - path] = 0;

	ret = kasprintf(GFP_KERNEL, "%s.%s%s", base, board_type, suffix);
	if (!ret)
		brcmf_err("out of memory allocating firmware path for '%s'\n",
			  path);

	brcmf_dbg(TRACE, "FW alt path: %s\n", ret);

	return ret;
}

static int brcmf_fw_request_firmware(const struct firmware **fw,
				     struct brcmf_fw *fwctx)
{
	struct brcmf_fw_item *cur = &fwctx->req->items[fwctx->curpos];
	unsigned int i;
	int ret;

	/* Files can be board-specific, first try board-specific paths */
	for (i = 0; i < ARRAY_SIZE(fwctx->req->board_types); i++) {
		char *alt_path;

		if (!fwctx->req->board_types[i])
			goto fallback;
		alt_path = brcm_alt_fw_path(cur->path,
					    fwctx->req->board_types[i]);
		if (!alt_path)
			goto fallback;

		ret = firmware_request_nowarn(fw, alt_path, fwctx->dev);
		kfree(alt_path);
		if (ret == 0)
			return ret;
	}

fallback:
	return request_firmware(fw, cur->path, fwctx->dev);
}

static void brcmf_fw_request_done(const struct firmware *fw, void *ctx)
{
	struct brcmf_fw *fwctx = ctx;
	int ret;

	ret = brcmf_fw_complete_request(fw, fwctx);

	while (ret == 0 && ++fwctx->curpos < fwctx->req->n_items) {
		brcmf_fw_request_firmware(&fw, fwctx);
		ret = brcmf_fw_complete_request(fw, ctx);
	}

	if (ret) {
		brcmf_fw_free_request(fwctx->req);
		fwctx->req = NULL;
	}
	fwctx->done(fwctx->dev, ret, fwctx->req);
	kfree(fwctx);
}

static void brcmf_fw_request_done_alt_path(const struct firmware *fw, void *ctx)
{
	struct brcmf_fw *fwctx = ctx;
	struct brcmf_fw_item *first = &fwctx->req->items[0];
	const char *board_type, *alt_path;
	int ret = 0;

	if (fw) {
		brcmf_fw_request_done(fw, ctx);
		return;
	}

	/* Try next board firmware */
	if (fwctx->board_index < ARRAY_SIZE(fwctx->req->board_types)) {
		board_type = fwctx->req->board_types[fwctx->board_index++];
		if (!board_type)
			goto fallback;
		alt_path = brcm_alt_fw_path(first->path, board_type);
		if (!alt_path)
			goto fallback;

		ret = request_firmware_nowait(THIS_MODULE, true, alt_path,
					      fwctx->dev, GFP_KERNEL, fwctx,
					      brcmf_fw_request_done_alt_path);
		kfree(alt_path);

		if (ret < 0)
			brcmf_fw_request_done(fw, ctx);
		return;
	}

fallback:
	/* Fall back to canonical path if board firmware not found */
	ret = request_firmware_nowait(THIS_MODULE, true, first->path,
				      fwctx->dev, GFP_KERNEL, fwctx,
				      brcmf_fw_request_done);

	if (ret < 0)
		brcmf_fw_request_done(fw, ctx);
}

static bool brcmf_fw_request_is_valid(struct brcmf_fw_request *req)
{
	struct brcmf_fw_item *item;
	int i;

	if (!req->n_items)
		return false;

	for (i = 0, item = &req->items[0]; i < req->n_items; i++, item++) {
		if (!item->path)
			return false;
	}
	return true;
}

int brcmf_fw_get_firmwares(struct device *dev, struct brcmf_fw_request *req,
			   void (*fw_cb)(struct device *dev, int err,
					 struct brcmf_fw_request *req))
{
	struct brcmf_fw_item *first = &req->items[0];
	struct brcmf_fw *fwctx;
	char *alt_path = NULL;
	int ret;

	brcmf_dbg(TRACE, "enter: dev=%s\n", dev_name(dev));
	if (!fw_cb)
		return -EINVAL;

	if (!brcmf_fw_request_is_valid(req))
		return -EINVAL;

	fwctx = kzalloc(sizeof(*fwctx), GFP_KERNEL);
	if (!fwctx)
		return -ENOMEM;

	fwctx->dev = dev;
	fwctx->req = req;
	fwctx->done = fw_cb;

	/* First try alternative board-specific path if any */
	if (fwctx->req->board_types[0])
		alt_path = brcm_alt_fw_path(first->path,
					    fwctx->req->board_types[0]);
	if (alt_path) {
		fwctx->board_index++;
		ret = request_firmware_nowait(THIS_MODULE, true, alt_path,
					      fwctx->dev, GFP_KERNEL, fwctx,
					      brcmf_fw_request_done_alt_path);
		kfree(alt_path);
	} else {
		ret = request_firmware_nowait(THIS_MODULE, true, first->path,
					      fwctx->dev, GFP_KERNEL, fwctx,
					      brcmf_fw_request_done);
	}
	if (ret < 0)
		brcmf_fw_request_done(NULL, fwctx);

	return 0;
}

struct brcmf_fw_request *
brcmf_fw_alloc_request(u32 chip, u32 chiprev,
		       const struct brcmf_firmware_mapping mapping_table[],
		       u32 table_size, struct brcmf_fw_name *fwnames,
		       u32 n_fwnames)
{
	struct brcmf_fw_request *fwreq;
	char chipname[12];
	const char *mp_path;
	size_t mp_path_len;
	u32 i, j;
	char end = '\0';

	if (chiprev >= BITS_PER_TYPE(u32)) {
		brcmf_err("Invalid chip revision %u\n", chiprev);
		return NULL;
	}

	for (i = 0; i < table_size; i++) {
		if (mapping_table[i].chipid == chip &&
		    mapping_table[i].revmask & BIT(chiprev))
			break;
	}

	brcmf_chip_name(chip, chiprev, chipname, sizeof(chipname));

	if (i == table_size) {
		brcmf_err("Unknown chip %s\n", chipname);
		return NULL;
	}

	fwreq = kzalloc(struct_size(fwreq, items, n_fwnames), GFP_KERNEL);
	if (!fwreq)
		return NULL;

	brcmf_info("using %s for chip %s\n",
		   mapping_table[i].fw_base, chipname);

	mp_path = brcmf_mp_global.firmware_path;
	mp_path_len = strnlen(mp_path, BRCMF_FW_ALTPATH_LEN);
	if (mp_path_len)
		end = mp_path[mp_path_len - 1];

	fwreq->n_items = n_fwnames;

	for (j = 0; j < n_fwnames; j++) {
		fwreq->items[j].path = fwnames[j].path;
		fwnames[j].path[0] = '\0';
		/* check if firmware path is provided by module parameter */
		if (brcmf_mp_global.firmware_path[0] != '\0') {
			strscpy(fwnames[j].path, mp_path,
				BRCMF_FW_NAME_LEN);

			if (end != '/') {
				strlcat(fwnames[j].path, "/",
					BRCMF_FW_NAME_LEN);
			}
		}
		strlcat(fwnames[j].path, mapping_table[i].fw_base,
			BRCMF_FW_NAME_LEN);
		strlcat(fwnames[j].path, fwnames[j].extension,
			BRCMF_FW_NAME_LEN);
		fwreq->items[j].path = fwnames[j].path;
	}

	return fwreq;
}
