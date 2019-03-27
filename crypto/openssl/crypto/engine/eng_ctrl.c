/*
 * Copyright 2001-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "eng_int.h"

/*
 * When querying a ENGINE-specific control command's 'description', this
 * string is used if the ENGINE_CMD_DEFN has cmd_desc set to NULL.
 */
static const char *int_no_description = "";

/*
 * These internal functions handle 'CMD'-related control commands when the
 * ENGINE in question has asked us to take care of it (ie. the ENGINE did not
 * set the ENGINE_FLAGS_MANUAL_CMD_CTRL flag.
 */

static int int_ctrl_cmd_is_null(const ENGINE_CMD_DEFN *defn)
{
    if ((defn->cmd_num == 0) || (defn->cmd_name == NULL))
        return 1;
    return 0;
}

static int int_ctrl_cmd_by_name(const ENGINE_CMD_DEFN *defn, const char *s)
{
    int idx = 0;
    while (!int_ctrl_cmd_is_null(defn) && (strcmp(defn->cmd_name, s) != 0)) {
        idx++;
        defn++;
    }
    if (int_ctrl_cmd_is_null(defn))
        /* The given name wasn't found */
        return -1;
    return idx;
}

static int int_ctrl_cmd_by_num(const ENGINE_CMD_DEFN *defn, unsigned int num)
{
    int idx = 0;
    /*
     * NB: It is stipulated that 'cmd_defn' lists are ordered by cmd_num. So
     * our searches don't need to take any longer than necessary.
     */
    while (!int_ctrl_cmd_is_null(defn) && (defn->cmd_num < num)) {
        idx++;
        defn++;
    }
    if (defn->cmd_num == num)
        return idx;
    /* The given cmd_num wasn't found */
    return -1;
}

static int int_ctrl_helper(ENGINE *e, int cmd, long i, void *p,
                           void (*f) (void))
{
    int idx;
    char *s = (char *)p;
    const ENGINE_CMD_DEFN *cdp;

    /* Take care of the easy one first (eg. it requires no searches) */
    if (cmd == ENGINE_CTRL_GET_FIRST_CMD_TYPE) {
        if ((e->cmd_defns == NULL) || int_ctrl_cmd_is_null(e->cmd_defns))
            return 0;
        return e->cmd_defns->cmd_num;
    }
    /* One or two commands require that "p" be a valid string buffer */
    if ((cmd == ENGINE_CTRL_GET_CMD_FROM_NAME) ||
        (cmd == ENGINE_CTRL_GET_NAME_FROM_CMD) ||
        (cmd == ENGINE_CTRL_GET_DESC_FROM_CMD)) {
        if (s == NULL) {
            ENGINEerr(ENGINE_F_INT_CTRL_HELPER, ERR_R_PASSED_NULL_PARAMETER);
            return -1;
        }
    }
    /* Now handle cmd_name -> cmd_num conversion */
    if (cmd == ENGINE_CTRL_GET_CMD_FROM_NAME) {
        if ((e->cmd_defns == NULL)
            || ((idx = int_ctrl_cmd_by_name(e->cmd_defns, s)) < 0)) {
            ENGINEerr(ENGINE_F_INT_CTRL_HELPER, ENGINE_R_INVALID_CMD_NAME);
            return -1;
        }
        return e->cmd_defns[idx].cmd_num;
    }
    /*
     * For the rest of the commands, the 'long' argument must specify a valid
     * command number - so we need to conduct a search.
     */
    if ((e->cmd_defns == NULL)
        || ((idx = int_ctrl_cmd_by_num(e->cmd_defns, (unsigned int)i)) < 0)) {
        ENGINEerr(ENGINE_F_INT_CTRL_HELPER, ENGINE_R_INVALID_CMD_NUMBER);
        return -1;
    }
    /* Now the logic splits depending on command type */
    cdp = &e->cmd_defns[idx];
    switch (cmd) {
    case ENGINE_CTRL_GET_NEXT_CMD_TYPE:
        cdp++;
        return int_ctrl_cmd_is_null(cdp) ? 0 : cdp->cmd_num;
    case ENGINE_CTRL_GET_NAME_LEN_FROM_CMD:
        return strlen(cdp->cmd_name);
    case ENGINE_CTRL_GET_NAME_FROM_CMD:
        return strlen(strcpy(s, cdp->cmd_name));
    case ENGINE_CTRL_GET_DESC_LEN_FROM_CMD:
        return strlen(cdp->cmd_desc == NULL ? int_no_description
                                            : cdp->cmd_desc);
    case ENGINE_CTRL_GET_DESC_FROM_CMD:
        return strlen(strcpy(s, cdp->cmd_desc == NULL ? int_no_description
                                                      : cdp->cmd_desc));
    case ENGINE_CTRL_GET_CMD_FLAGS:
        return cdp->cmd_flags;
    }
    /* Shouldn't really be here ... */
    ENGINEerr(ENGINE_F_INT_CTRL_HELPER, ENGINE_R_INTERNAL_LIST_ERROR);
    return -1;
}

int ENGINE_ctrl(ENGINE *e, int cmd, long i, void *p, void (*f) (void))
{
    int ctrl_exists, ref_exists;
    if (e == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_CTRL, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    CRYPTO_THREAD_write_lock(global_engine_lock);
    ref_exists = ((e->struct_ref > 0) ? 1 : 0);
    CRYPTO_THREAD_unlock(global_engine_lock);
    ctrl_exists = ((e->ctrl == NULL) ? 0 : 1);
    if (!ref_exists) {
        ENGINEerr(ENGINE_F_ENGINE_CTRL, ENGINE_R_NO_REFERENCE);
        return 0;
    }
    /*
     * Intercept any "root-level" commands before trying to hand them on to
     * ctrl() handlers.
     */
    switch (cmd) {
    case ENGINE_CTRL_HAS_CTRL_FUNCTION:
        return ctrl_exists;
    case ENGINE_CTRL_GET_FIRST_CMD_TYPE:
    case ENGINE_CTRL_GET_NEXT_CMD_TYPE:
    case ENGINE_CTRL_GET_CMD_FROM_NAME:
    case ENGINE_CTRL_GET_NAME_LEN_FROM_CMD:
    case ENGINE_CTRL_GET_NAME_FROM_CMD:
    case ENGINE_CTRL_GET_DESC_LEN_FROM_CMD:
    case ENGINE_CTRL_GET_DESC_FROM_CMD:
    case ENGINE_CTRL_GET_CMD_FLAGS:
        if (ctrl_exists && !(e->flags & ENGINE_FLAGS_MANUAL_CMD_CTRL))
            return int_ctrl_helper(e, cmd, i, p, f);
        if (!ctrl_exists) {
            ENGINEerr(ENGINE_F_ENGINE_CTRL, ENGINE_R_NO_CONTROL_FUNCTION);
            /*
             * For these cmd-related functions, failure is indicated by a -1
             * return value (because 0 is used as a valid return in some
             * places).
             */
            return -1;
        }
    default:
        break;
    }
    /* Anything else requires a ctrl() handler to exist. */
    if (!ctrl_exists) {
        ENGINEerr(ENGINE_F_ENGINE_CTRL, ENGINE_R_NO_CONTROL_FUNCTION);
        return 0;
    }
    return e->ctrl(e, cmd, i, p, f);
}

int ENGINE_cmd_is_executable(ENGINE *e, int cmd)
{
    int flags;
    if ((flags =
         ENGINE_ctrl(e, ENGINE_CTRL_GET_CMD_FLAGS, cmd, NULL, NULL)) < 0) {
        ENGINEerr(ENGINE_F_ENGINE_CMD_IS_EXECUTABLE,
                  ENGINE_R_INVALID_CMD_NUMBER);
        return 0;
    }
    if (!(flags & ENGINE_CMD_FLAG_NO_INPUT) &&
        !(flags & ENGINE_CMD_FLAG_NUMERIC) &&
        !(flags & ENGINE_CMD_FLAG_STRING))
        return 0;
    return 1;
}

int ENGINE_ctrl_cmd(ENGINE *e, const char *cmd_name,
                    long i, void *p, void (*f) (void), int cmd_optional)
{
    int num;

    if (e == NULL || cmd_name == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_CTRL_CMD, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    if (e->ctrl == NULL
        || (num = ENGINE_ctrl(e, ENGINE_CTRL_GET_CMD_FROM_NAME,
                              0, (void *)cmd_name, NULL)) <= 0) {
        /*
         * If the command didn't *have* to be supported, we fake success.
         * This allows certain settings to be specified for multiple ENGINEs
         * and only require a change of ENGINE id (without having to
         * selectively apply settings). Eg. changing from a hardware device
         * back to the regular software ENGINE without editing the config
         * file, etc.
         */
        if (cmd_optional) {
            ERR_clear_error();
            return 1;
        }
        ENGINEerr(ENGINE_F_ENGINE_CTRL_CMD, ENGINE_R_INVALID_CMD_NAME);
        return 0;
    }
    /*
     * Force the result of the control command to 0 or 1, for the reasons
     * mentioned before.
     */
    if (ENGINE_ctrl(e, num, i, p, f) > 0)
        return 1;
    return 0;
}

int ENGINE_ctrl_cmd_string(ENGINE *e, const char *cmd_name, const char *arg,
                           int cmd_optional)
{
    int num, flags;
    long l;
    char *ptr;

    if (e == NULL || cmd_name == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_CTRL_CMD_STRING, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    if (e->ctrl == NULL
        || (num = ENGINE_ctrl(e, ENGINE_CTRL_GET_CMD_FROM_NAME,
                              0, (void *)cmd_name, NULL)) <= 0) {
        /*
         * If the command didn't *have* to be supported, we fake success.
         * This allows certain settings to be specified for multiple ENGINEs
         * and only require a change of ENGINE id (without having to
         * selectively apply settings). Eg. changing from a hardware device
         * back to the regular software ENGINE without editing the config
         * file, etc.
         */
        if (cmd_optional) {
            ERR_clear_error();
            return 1;
        }
        ENGINEerr(ENGINE_F_ENGINE_CTRL_CMD_STRING, ENGINE_R_INVALID_CMD_NAME);
        return 0;
    }
    if (!ENGINE_cmd_is_executable(e, num)) {
        ENGINEerr(ENGINE_F_ENGINE_CTRL_CMD_STRING,
                  ENGINE_R_CMD_NOT_EXECUTABLE);
        return 0;
    }

    flags = ENGINE_ctrl(e, ENGINE_CTRL_GET_CMD_FLAGS, num, NULL, NULL);
    if (flags < 0) {
        /*
         * Shouldn't happen, given that ENGINE_cmd_is_executable() returned
         * success.
         */
        ENGINEerr(ENGINE_F_ENGINE_CTRL_CMD_STRING,
                  ENGINE_R_INTERNAL_LIST_ERROR);
        return 0;
    }
    /*
     * If the command takes no input, there must be no input. And vice versa.
     */
    if (flags & ENGINE_CMD_FLAG_NO_INPUT) {
        if (arg != NULL) {
            ENGINEerr(ENGINE_F_ENGINE_CTRL_CMD_STRING,
                      ENGINE_R_COMMAND_TAKES_NO_INPUT);
            return 0;
        }
        /*
         * We deliberately force the result of ENGINE_ctrl() to 0 or 1 rather
         * than returning it as "return data". This is to ensure usage of
         * these commands is consistent across applications and that certain
         * applications don't understand it one way, and others another.
         */
        if (ENGINE_ctrl(e, num, 0, (void *)arg, NULL) > 0)
            return 1;
        return 0;
    }
    /* So, we require input */
    if (arg == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_CTRL_CMD_STRING,
                  ENGINE_R_COMMAND_TAKES_INPUT);
        return 0;
    }
    /* If it takes string input, that's easy */
    if (flags & ENGINE_CMD_FLAG_STRING) {
        /* Same explanation as above */
        if (ENGINE_ctrl(e, num, 0, (void *)arg, NULL) > 0)
            return 1;
        return 0;
    }
    /*
     * If it doesn't take numeric either, then it is unsupported for use in a
     * config-setting situation, which is what this function is for. This
     * should never happen though, because ENGINE_cmd_is_executable() was
     * used.
     */
    if (!(flags & ENGINE_CMD_FLAG_NUMERIC)) {
        ENGINEerr(ENGINE_F_ENGINE_CTRL_CMD_STRING,
                  ENGINE_R_INTERNAL_LIST_ERROR);
        return 0;
    }
    l = strtol(arg, &ptr, 10);
    if ((arg == ptr) || (*ptr != '\0')) {
        ENGINEerr(ENGINE_F_ENGINE_CTRL_CMD_STRING,
                  ENGINE_R_ARGUMENT_IS_NOT_A_NUMBER);
        return 0;
    }
    /*
     * Force the result of the control command to 0 or 1, for the reasons
     * mentioned before.
     */
    if (ENGINE_ctrl(e, num, l, NULL, NULL) > 0)
        return 1;
    return 0;
}
