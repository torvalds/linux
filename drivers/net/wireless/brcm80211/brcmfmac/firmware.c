/*
 * Copyright (c) 2013 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/firmware.h>

#include "dhd_dbg.h"
#include "firmware.h"

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
 * @fwnv: input buffer being parsed.
 * @nvram: output buffer with parse result.
 * @nvram_len: lenght of parse result.
 * @line: current line.
 * @column: current column in line.
 * @pos: byte offset in input buffer.
 * @entry: start position of key,value entry.
 */
struct nvram_parser {
	enum nvram_parser_state state;
	const struct firmware *fwnv;
	u8 *nvram;
	u32 nvram_len;
	u32 line;
	u32 column;
	u32 pos;
	u32 entry;
};

static bool is_nvram_char(char c)
{
	/* comment marker excluded */
	if (c == '#')
		return false;

	/* key and value may have any other readable character */
	return (c > 0x20 && c < 0x7f);
}

static bool is_whitespace(char c)
{
	return (c == ' ' || c == '\r' || c == '\n' || c == '\t');
}

static enum nvram_parser_state brcmf_nvram_handle_idle(struct nvram_parser *nvp)
{
	char c;

	c = nvp->fwnv->data[nvp->pos];
	if (c == '\n')
		return COMMENT;
	if (is_whitespace(c))
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

	c = nvp->fwnv->data[nvp->pos];
	if (c == '=') {
		st = VALUE;
	} else if (!is_nvram_char(c)) {
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

	c = nvp->fwnv->data[nvp->pos];
	if (!is_nvram_char(c)) {
		/* key,value pair complete */
		ekv = (u8 *)&nvp->fwnv->data[nvp->pos];
		skv = (u8 *)&nvp->fwnv->data[nvp->entry];
		cplen = ekv - skv;
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
	char *eol, *sol;

	sol = (char *)&nvp->fwnv->data[nvp->pos];
	eol = strchr(sol, '\n');
	if (eol == NULL)
		return END;

	/* eat all moving to next line */
	nvp->line++;
	nvp->column = 1;
	nvp->pos += (eol - sol) + 1;
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
				   const struct firmware *nv)
{
	memset(nvp, 0, sizeof(*nvp));
	nvp->fwnv = nv;
	/* Alloc for extra 0 byte + roundup by 4 + length field */
	nvp->nvram = kzalloc(nv->size + 1 + 3 + sizeof(u32), GFP_KERNEL);
	if (!nvp->nvram)
		return -ENOMEM;

	nvp->line = 1;
	nvp->column = 1;
	return 0;
}

/* brcmf_nvram_strip :Takes a buffer of "<var>=<value>\n" lines read from a fil
 * and ending in a NUL. Removes carriage returns, empty lines, comment lines,
 * and converts newlines to NULs. Shortens buffer as needed and pads with NULs.
 * End of buffer is completed with token identifying length of buffer.
 */
void *brcmf_fw_nvram_strip(const struct firmware *nv, u32 *new_length)
{
	struct nvram_parser nvp;
	u32 pad;
	u32 token;
	__le32 token_le;

	if (brcmf_init_nvram_parser(&nvp, nv) < 0)
		return NULL;

	while (nvp.pos < nv->size) {
		nvp.state = nv_parser_states[nvp.state](&nvp);
		if (nvp.state == END)
			break;
	}
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

