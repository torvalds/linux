/*
 * Common hostapd/wpa_supplicant command line interface functions
 * Copyright (c) 2004-2016, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "utils/common.h"
#include "common/cli.h"


const char *const cli_license =
"This software may be distributed under the terms of the BSD license.\n"
"See README for more details.\n";

const char *const cli_full_license =
"This software may be distributed under the terms of the BSD license.\n"
"\n"
"Redistribution and use in source and binary forms, with or without\n"
"modification, are permitted provided that the following conditions are\n"
"met:\n"
"\n"
"1. Redistributions of source code must retain the above copyright\n"
"   notice, this list of conditions and the following disclaimer.\n"
"\n"
"2. Redistributions in binary form must reproduce the above copyright\n"
"   notice, this list of conditions and the following disclaimer in the\n"
"   documentation and/or other materials provided with the distribution.\n"
"\n"
"3. Neither the name(s) of the above-listed copyright holder(s) nor the\n"
"   names of its contributors may be used to endorse or promote products\n"
"   derived from this software without specific prior written permission.\n"
"\n"
"THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS\n"
"\"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT\n"
"LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR\n"
"A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT\n"
"OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,\n"
"SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT\n"
"LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,\n"
"DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY\n"
"THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n"
"(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE\n"
"OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n"
"\n";


void cli_txt_list_free(struct cli_txt_entry *e)
{
	dl_list_del(&e->list);
	os_free(e->txt);
	os_free(e);
}


void cli_txt_list_flush(struct dl_list *list)
{
	struct cli_txt_entry *e;

	while ((e = dl_list_first(list, struct cli_txt_entry, list)))
		cli_txt_list_free(e);
}


struct cli_txt_entry * cli_txt_list_get(struct dl_list *txt_list,
					const char *txt)
{
	struct cli_txt_entry *e;

	dl_list_for_each(e, txt_list, struct cli_txt_entry, list) {
		if (os_strcmp(e->txt, txt) == 0)
			return e;
	}
	return NULL;
}


void cli_txt_list_del(struct dl_list *txt_list, const char *txt)
{
	struct cli_txt_entry *e;

	e = cli_txt_list_get(txt_list, txt);
	if (e)
		cli_txt_list_free(e);
}


void cli_txt_list_del_addr(struct dl_list *txt_list, const char *txt)
{
	u8 addr[ETH_ALEN];
	char buf[18];

	if (hwaddr_aton(txt, addr) < 0)
		return;
	os_snprintf(buf, sizeof(buf), MACSTR, MAC2STR(addr));
	cli_txt_list_del(txt_list, buf);
}


void cli_txt_list_del_word(struct dl_list *txt_list, const char *txt,
			   int separator)
{
	const char *end;
	char *buf;

	end = os_strchr(txt, separator);
	if (end == NULL)
		end = txt + os_strlen(txt);
	buf = dup_binstr(txt, end - txt);
	if (buf == NULL)
		return;
	cli_txt_list_del(txt_list, buf);
	os_free(buf);
}


int cli_txt_list_add(struct dl_list *txt_list, const char *txt)
{
	struct cli_txt_entry *e;

	e = cli_txt_list_get(txt_list, txt);
	if (e)
		return 0;
	e = os_zalloc(sizeof(*e));
	if (e == NULL)
		return -1;
	e->txt = os_strdup(txt);
	if (e->txt == NULL) {
		os_free(e);
		return -1;
	}
	dl_list_add(txt_list, &e->list);
	return 0;
}


int cli_txt_list_add_addr(struct dl_list *txt_list, const char *txt)
{
	u8 addr[ETH_ALEN];
	char buf[18];

	if (hwaddr_aton(txt, addr) < 0)
		return -1;
	os_snprintf(buf, sizeof(buf), MACSTR, MAC2STR(addr));
	return cli_txt_list_add(txt_list, buf);
}


int cli_txt_list_add_word(struct dl_list *txt_list, const char *txt,
			  int separator)
{
	const char *end;
	char *buf;
	int ret;

	end = os_strchr(txt, separator);
	if (end == NULL)
		end = txt + os_strlen(txt);
	buf = dup_binstr(txt, end - txt);
	if (buf == NULL)
		return -1;
	ret = cli_txt_list_add(txt_list, buf);
	os_free(buf);
	return ret;
}


char ** cli_txt_list_array(struct dl_list *txt_list)
{
	unsigned int i, count = dl_list_len(txt_list);
	char **res;
	struct cli_txt_entry *e;

	res = os_calloc(count + 1, sizeof(char *));
	if (res == NULL)
		return NULL;

	i = 0;
	dl_list_for_each(e, txt_list, struct cli_txt_entry, list) {
		res[i] = os_strdup(e->txt);
		if (res[i] == NULL)
			break;
		i++;
	}

	return res;
}


int get_cmd_arg_num(const char *str, int pos)
{
	int arg = 0, i;

	for (i = 0; i <= pos; i++) {
		if (str[i] != ' ') {
			arg++;
			while (i <= pos && str[i] != ' ')
				i++;
		}
	}

	if (arg > 0)
		arg--;
	return arg;
}


int write_cmd(char *buf, size_t buflen, const char *cmd, int argc, char *argv[])
{
	int i, res;
	char *pos, *end;

	pos = buf;
	end = buf + buflen;

	res = os_snprintf(pos, end - pos, "%s", cmd);
	if (os_snprintf_error(end - pos, res))
		goto fail;
	pos += res;

	for (i = 0; i < argc; i++) {
		res = os_snprintf(pos, end - pos, " %s", argv[i]);
		if (os_snprintf_error(end - pos, res))
			goto fail;
		pos += res;
	}

	buf[buflen - 1] = '\0';
	return 0;

fail:
	printf("Too long command\n");
	return -1;
}


int tokenize_cmd(char *cmd, char *argv[])
{
	char *pos;
	int argc = 0;

	pos = cmd;
	for (;;) {
		while (*pos == ' ')
			pos++;
		if (*pos == '\0')
			break;
		argv[argc] = pos;
		argc++;
		if (argc == max_args)
			break;
		if (*pos == '"') {
			char *pos2 = os_strrchr(pos, '"');
			if (pos2)
				pos = pos2 + 1;
		}
		while (*pos != '\0' && *pos != ' ')
			pos++;
		if (*pos == ' ')
			*pos++ = '\0';
	}

	return argc;
}
