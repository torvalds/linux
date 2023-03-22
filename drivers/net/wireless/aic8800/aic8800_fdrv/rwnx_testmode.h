/**
 ****************************************************************************************
 *
 * @file rwnx_testmode.h
 *
 * @brief Test mode function declarations
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ****************************************************************************************
 */

#ifndef RWNX_TESTMODE_H_
#define RWNX_TESTMODE_H_

#include <net/mac80211.h>
#include <net/netlink.h>

/* Commands from user space to kernel space(RWNX_TM_CMD_APP2DEV_XX) and
 * from and kernel space to user space(RWNX_TM_CMD_DEV2APP_XX).
 * The command ID is carried with RWNX_TM_ATTR_COMMAND.
 */
enum rwnx_tm_cmd_t {
    /* commands from user application to access register */
    RWNX_TM_CMD_APP2DEV_REG_READ = 1,
    RWNX_TM_CMD_APP2DEV_REG_WRITE,

    /* commands from user application to select the Debug levels */
    RWNX_TM_CMD_APP2DEV_SET_DBGMODFILTER,
    RWNX_TM_CMD_APP2DEV_SET_DBGSEVFILTER,

    /* commands to access registers without sending messages to LMAC layer,
     * this must be used when LMAC FW is stuck. */
    RWNX_TM_CMD_APP2DEV_REG_READ_DBG,
    RWNX_TM_CMD_APP2DEV_REG_WRITE_DBG,

    RWNX_TM_CMD_MAX,
};

enum rwnx_tm_attr_t {
    RWNX_TM_ATTR_NOT_APPLICABLE = 0,

    RWNX_TM_ATTR_COMMAND,

    /* When RWNX_TM_ATTR_COMMAND is RWNX_TM_CMD_APP2DEV_REG_XXX,
     * The mandatory fields are:
     * RWNX_TM_ATTR_REG_OFFSET for the offset of the target register;
     * RWNX_TM_ATTR_REG_VALUE32 for value */
    RWNX_TM_ATTR_REG_OFFSET,
    RWNX_TM_ATTR_REG_VALUE32,

    /* When RWNX_TM_ATTR_COMMAND is RWNX_TM_CMD_APP2DEV_SET_DBGXXXFILTER,
     * The mandatory field is RWNX_TM_ATTR_REG_FILTER. */
    RWNX_TM_ATTR_REG_FILTER,

    RWNX_TM_ATTR_MAX,
};

/***********************************************************************/
int rwnx_testmode_reg(struct ieee80211_hw *hw, struct nlattr **tb);
int rwnx_testmode_dbg_filter(struct ieee80211_hw *hw, struct nlattr **tb);
int rwnx_testmode_reg_dbg(struct ieee80211_hw *hw, struct nlattr **tb);

#endif /* RWNX_TESTMODE_H_ */
