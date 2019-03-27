/*
 * Common hostapd/wpa_supplicant command line interface functionality
 * Copyright (c) 2004-2016, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef CLI_H
#define CLI_H

#include "utils/list.h"

extern const char *const cli_license;
extern const char *const cli_full_license;

struct cli_txt_entry {
	struct dl_list list;
	char *txt;
};

void cli_txt_list_free(struct cli_txt_entry *e);
void cli_txt_list_flush(struct dl_list *list);

struct cli_txt_entry *
cli_txt_list_get(struct dl_list *txt_list, const char *txt);

void cli_txt_list_del(struct dl_list *txt_list, const char *txt);
void cli_txt_list_del_addr(struct dl_list *txt_list, const char *txt);
void cli_txt_list_del_word(struct dl_list *txt_list, const char *txt,
			   int separator);

int cli_txt_list_add(struct dl_list *txt_list, const char *txt);
int cli_txt_list_add_addr(struct dl_list *txt_list, const char *txt);
int cli_txt_list_add_word(struct dl_list *txt_list, const char *txt,
			  int separator);

char ** cli_txt_list_array(struct dl_list *txt_list);

int get_cmd_arg_num(const char *str, int pos);
int write_cmd(char *buf, size_t buflen, const char *cmd, int argc,
	      char *argv[]);

#define max_args 10
int tokenize_cmd(char *cmd, char *argv[]);

#endif /* CLI_H */
