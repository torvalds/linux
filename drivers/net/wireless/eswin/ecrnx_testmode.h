/**
 ****************************************************************************************
 *
 * @file ecrnx_testmode.h
 *
 * @brief Test mode function declarations
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ****************************************************************************************
 */

#ifndef ECRNX_TESTMODE_H_
#define ECRNX_TESTMODE_H_

#include <net/mac80211.h>
#include <net/netlink.h>

/* Commands from user space to kernel space(ECRNX_TM_CMD_APP2DEV_XX) and
 * from and kernel space to user space(ECRNX_TM_CMD_DEV2APP_XX).
 * The command ID is carried with ECRNX_TM_ATTR_COMMAND.
 */
enum ecrnx_tm_cmd_t {
    /* commands from user application to access register */
    ECRNX_TM_CMD_APP2DEV_REG_READ = 1,
    ECRNX_TM_CMD_APP2DEV_REG_WRITE,

    /* commands from user application to select the Debug levels */
    ECRNX_TM_CMD_APP2DEV_SET_DBGMODFILTER,
    ECRNX_TM_CMD_APP2DEV_SET_DBGSEVFILTER,

    /* commands to access registers without sending messages to LMAC layer,
     * this must be used when LMAC FW is stuck. */
    ECRNX_TM_CMD_APP2DEV_REG_READ_DBG,
    ECRNX_TM_CMD_APP2DEV_REG_WRITE_DBG,

    ECRNX_TM_CMD_MAX,
};

enum ecrnx_tm_attr_t {
    ECRNX_TM_ATTR_NOT_APPLICABLE = 0,

    ECRNX_TM_ATTR_COMMAND,

    /* When ECRNX_TM_ATTR_COMMAND is ECRNX_TM_CMD_APP2DEV_REG_XXX,
     * The mandatory fields are:
     * ECRNX_TM_ATTR_REG_OFFSET for the offset of the target register;
     * ECRNX_TM_ATTR_REG_VALUE32 for value */
    ECRNX_TM_ATTR_REG_OFFSET,
    ECRNX_TM_ATTR_REG_VALUE32,

    /* When ECRNX_TM_ATTR_COMMAND is ECRNX_TM_CMD_APP2DEV_SET_DBGXXXFILTER,
     * The mandatory field is ECRNX_TM_ATTR_REG_FILTER. */
    ECRNX_TM_ATTR_REG_FILTER,

    ECRNX_TM_ATTR_MAX,
};

/***********************************************************************/
int ecrnx_testmode_reg(struct ieee80211_hw *hw, struct nlattr **tb);
int ecrnx_testmode_dbg_filter(struct ieee80211_hw *hw, struct nlattr **tb);
int ecrnx_testmode_reg_dbg(struct ieee80211_hw *hw, struct nlattr **tb);

#endif /* ECRNX_TESTMODE_H_ */
