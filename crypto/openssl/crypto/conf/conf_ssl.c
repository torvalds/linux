/*
 * Copyright 2015-2019 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <string.h>
#include <openssl/conf.h>
#include <openssl/err.h>
#include "internal/sslconf.h"
#include "conf_lcl.h"

/*
 * SSL library configuration module placeholder. We load it here but defer
 * all decisions about its contents to libssl.
 */

struct ssl_conf_name_st {
    /* Name of this set of commands */
    char *name;
    /* List of commands */
    SSL_CONF_CMD *cmds;
    /* Number of commands */
    size_t cmd_count;
};

struct ssl_conf_cmd_st {
    /* Command */
    char *cmd;
    /* Argument */
    char *arg;
};

static struct ssl_conf_name_st *ssl_names;
static size_t ssl_names_count;

static void ssl_module_free(CONF_IMODULE *md)
{
    size_t i, j;
    if (ssl_names == NULL)
        return;
    for (i = 0; i < ssl_names_count; i++) {
        struct ssl_conf_name_st *tname = ssl_names + i;

        OPENSSL_free(tname->name);
        for (j = 0; j < tname->cmd_count; j++) {
            OPENSSL_free(tname->cmds[j].cmd);
            OPENSSL_free(tname->cmds[j].arg);
        }
        OPENSSL_free(tname->cmds);
    }
    OPENSSL_free(ssl_names);
    ssl_names = NULL;
    ssl_names_count = 0;
}

static int ssl_module_init(CONF_IMODULE *md, const CONF *cnf)
{
    size_t i, j, cnt;
    int rv = 0;
    const char *ssl_conf_section;
    STACK_OF(CONF_VALUE) *cmd_lists;

    ssl_conf_section = CONF_imodule_get_value(md);
    cmd_lists = NCONF_get_section(cnf, ssl_conf_section);
    if (sk_CONF_VALUE_num(cmd_lists) <= 0) {
        if (cmd_lists == NULL)
            CONFerr(CONF_F_SSL_MODULE_INIT, CONF_R_SSL_SECTION_NOT_FOUND);
        else
            CONFerr(CONF_F_SSL_MODULE_INIT, CONF_R_SSL_SECTION_EMPTY);
        ERR_add_error_data(2, "section=", ssl_conf_section);
        goto err;
    }
    cnt = sk_CONF_VALUE_num(cmd_lists);
    ssl_module_free(md);
    ssl_names = OPENSSL_zalloc(sizeof(*ssl_names) * cnt);
    if (ssl_names == NULL)
        goto err;
    ssl_names_count = cnt;
    for (i = 0; i < ssl_names_count; i++) {
        struct ssl_conf_name_st *ssl_name = ssl_names + i;
        CONF_VALUE *sect = sk_CONF_VALUE_value(cmd_lists, (int)i);
        STACK_OF(CONF_VALUE) *cmds = NCONF_get_section(cnf, sect->value);

        if (sk_CONF_VALUE_num(cmds) <= 0) {
            if (cmds == NULL)
                CONFerr(CONF_F_SSL_MODULE_INIT,
                        CONF_R_SSL_COMMAND_SECTION_NOT_FOUND);
            else
                CONFerr(CONF_F_SSL_MODULE_INIT,
                        CONF_R_SSL_COMMAND_SECTION_EMPTY);
            ERR_add_error_data(4, "name=", sect->name, ", value=", sect->value);
            goto err;
        }
        ssl_name->name = OPENSSL_strdup(sect->name);
        if (ssl_name->name == NULL)
            goto err;
        cnt = sk_CONF_VALUE_num(cmds);
        ssl_name->cmds = OPENSSL_zalloc(cnt * sizeof(struct ssl_conf_cmd_st));
        if (ssl_name->cmds == NULL)
            goto err;
        ssl_name->cmd_count = cnt;
        for (j = 0; j < cnt; j++) {
            const char *name;
            CONF_VALUE *cmd_conf = sk_CONF_VALUE_value(cmds, (int)j);
            struct ssl_conf_cmd_st *cmd = ssl_name->cmds + j;

            /* Skip any initial dot in name */
            name = strchr(cmd_conf->name, '.');
            if (name != NULL)
                name++;
            else
                name = cmd_conf->name;
            cmd->cmd = OPENSSL_strdup(name);
            cmd->arg = OPENSSL_strdup(cmd_conf->value);
            if (cmd->cmd == NULL || cmd->arg == NULL)
                goto err;
        }

    }
    rv = 1;
 err:
    if (rv == 0)
        ssl_module_free(md);
    return rv;
}

/*
 * Returns the set of commands with index |idx| previously searched for via
 * conf_ssl_name_find. Also stores the name of the set of commands in |*name|
 * and the number of commands in the set in |*cnt|.
 */
const SSL_CONF_CMD *conf_ssl_get(size_t idx, const char **name, size_t *cnt)
{
    *name = ssl_names[idx].name;
    *cnt = ssl_names[idx].cmd_count;
    return ssl_names[idx].cmds;
}

/*
 * Search for the named set of commands given in |name|. On success return the
 * index for the command set in |*idx|.
 * Returns 1 on success or 0 on failure.
 */
int conf_ssl_name_find(const char *name, size_t *idx)
{
    size_t i;
    const struct ssl_conf_name_st *nm;

    if (name == NULL)
        return 0;
    for (i = 0, nm = ssl_names; i < ssl_names_count; i++, nm++) {
        if (strcmp(nm->name, name) == 0) {
            *idx = i;
            return 1;
        }
    }
    return 0;
}

/*
 * Given a command set |cmd|, return details on the command at index |idx| which
 * must be less than the number of commands in the set (as returned by
 * conf_ssl_get). The name of the command will be returned in |*cmdstr| and the
 * argument is returned in |*arg|.
 */
void conf_ssl_get_cmd(const SSL_CONF_CMD *cmd, size_t idx, char **cmdstr,
                      char **arg)
{
    *cmdstr = cmd[idx].cmd;
    *arg = cmd[idx].arg;
}

void conf_add_ssl_module(void)
{
    CONF_module_add("ssl_conf", ssl_module_init, ssl_module_free);
}
