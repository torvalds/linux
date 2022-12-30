/**
 ******************************************************************************
 *
 * ecrnx_cmds.c
 *
 * Handles queueing (push to IPC, ack/cfm from IPC) of commands issued to
 * LMAC FW
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#include <linux/list.h>

#include "ecrnx_cmds.h"
#include "ecrnx_defs.h"
#include "ecrnx_strs.h"
#define CREATE_TRACE_POINTS
#include "ecrnx_events.h"

/**
 *
 */
static void cmd_dump(const struct ecrnx_cmd *cmd)
{
#ifndef CONFIG_ECRNX_FHOST
    ECRNX_PRINT("tkn[%d]  flags:%04x  result:%3d  cmd:%4d-%-24s - reqcfm(%4d-%-s)\n",
           cmd->tkn, cmd->flags, cmd->result, cmd->id, ECRNX_ID2STR(cmd->id),
           cmd->reqid, cmd->reqid != (lmac_msg_id_t)-1 ? ECRNX_ID2STR(cmd->reqid) : "none");
#endif
}

/**
 *
 */
static void cmd_complete(struct ecrnx_cmd_mgr *cmd_mgr, struct ecrnx_cmd *cmd)
{
    lockdep_assert_held(&cmd_mgr->lock);

    list_del(&cmd->list);
    cmd_mgr->queue_sz--;

    cmd->flags |= ECRNX_CMD_FLAG_DONE;
    if (cmd->flags & ECRNX_CMD_FLAG_NONBLOCK) {
        kfree(cmd);
    } else {
        if (ECRNX_CMD_WAIT_COMPLETE(cmd->flags)) {
            cmd->result = 0;
            complete(&cmd->complete);
        }
    }
}

/**
 *
 */
static int cmd_mgr_queue(struct ecrnx_cmd_mgr *cmd_mgr, struct ecrnx_cmd *cmd)
{
    struct ecrnx_hw *ecrnx_hw = container_of(cmd_mgr, struct ecrnx_hw, cmd_mgr);
    bool defer_push = false;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);
    trace_msg_send(cmd->id);

    spin_lock_bh(&cmd_mgr->lock);

    if (cmd_mgr->state == ECRNX_CMD_MGR_STATE_CRASHED) {
        ECRNX_PRINT(KERN_CRIT"cmd queue crashed\n");
        cmd->result = -EPIPE;
        spin_unlock_bh(&cmd_mgr->lock);
        return -EPIPE;
    }

    #ifndef CONFIG_ECRNX_FHOST
    if (!list_empty(&cmd_mgr->cmds)) {
        struct ecrnx_cmd *last;

        if (cmd_mgr->queue_sz == cmd_mgr->max_queue_sz) {
            ECRNX_ERR(KERN_CRIT"Too many cmds (%d) already queued\n",
                   cmd_mgr->max_queue_sz);
            cmd->result = -ENOMEM;
            spin_unlock_bh(&cmd_mgr->lock);
            return -ENOMEM;
        }
        last = list_entry(cmd_mgr->cmds.prev, struct ecrnx_cmd, list);
        if (last->flags & (ECRNX_CMD_FLAG_WAIT_ACK | ECRNX_CMD_FLAG_WAIT_PUSH)) {
#if 0 // queue even NONBLOCK command.
            if (cmd->flags & ECRNX_CMD_FLAG_NONBLOCK) {
                printk(KERN_CRIT"cmd queue busy\n");
                cmd->result = -EBUSY;
                spin_unlock_bh(&cmd_mgr->lock);
                return -EBUSY;
            }
#endif
            cmd->flags |= ECRNX_CMD_FLAG_WAIT_PUSH;
            defer_push = true;
        }
    }
    #endif

    cmd->flags |= ECRNX_CMD_FLAG_WAIT_ACK;
    if (cmd->flags & ECRNX_CMD_FLAG_REQ_CFM)
        cmd->flags |= ECRNX_CMD_FLAG_WAIT_CFM;

    cmd->tkn    = cmd_mgr->next_tkn++;
    cmd->result = -EINTR;

    if (!(cmd->flags & ECRNX_CMD_FLAG_NONBLOCK))
        init_completion(&cmd->complete);

    list_add_tail(&cmd->list, &cmd_mgr->cmds);
    cmd_mgr->queue_sz++;
    spin_unlock_bh(&cmd_mgr->lock);

    if (!defer_push) {
        ecrnx_ipc_msg_push(ecrnx_hw, cmd, ECRNX_CMD_A2EMSG_LEN(cmd->a2e_msg));
        kfree(cmd->a2e_msg);
    }

    if (!(cmd->flags & ECRNX_CMD_FLAG_NONBLOCK)) {
        #ifdef CONFIG_ECRNX_FHOST
        if (wait_for_completion_killable(&cmd->complete)) {
            if (cmd->flags & ECRNX_CMD_FLAG_WAIT_ACK)
                up(&ecrnx_hw->term.fw_cmd);
            cmd->result = -EINTR;
            spin_lock_bh(&cmd_mgr->lock);
            cmd_complete(cmd_mgr, cmd);
            spin_unlock_bh(&cmd_mgr->lock);
            /* TODO: kill the cmd at fw level */
        } else {
            if (cmd->flags & ECRNX_CMD_FLAG_WAIT_ACK)
                up(&ecrnx_hw->term.fw_cmd);
        }
        #else
        unsigned long tout = msecs_to_jiffies(ECRNX_80211_CMD_TIMEOUT_MS * cmd_mgr->queue_sz);
        if (!wait_for_completion_killable_timeout(&cmd->complete, tout)) {
            ECRNX_PRINT("cmd timed-out queue_sz:%d\n",cmd_mgr->queue_sz);
            cmd_dump(cmd);
            spin_lock_bh(&cmd_mgr->lock);
            //cmd_mgr->state = ECRNX_CMD_MGR_STATE_CRASHED;
            if (!(cmd->flags & ECRNX_CMD_FLAG_DONE)) {
                cmd->result = -ETIMEDOUT;
                cmd_complete(cmd_mgr, cmd);
            }
            spin_unlock_bh(&cmd_mgr->lock);
        }
        #endif
    } else {
        cmd->result = 0;
    }

    return 0;
}

/**
 *
 */
static int cmd_mgr_llind(struct ecrnx_cmd_mgr *cmd_mgr, struct ecrnx_cmd *cmd)
{
    struct ecrnx_cmd *cur, *acked = NULL, *next = NULL;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    spin_lock(&cmd_mgr->lock);
    list_for_each_entry(cur, &cmd_mgr->cmds, list) {
        if (!acked) {
            if (cur->tkn == cmd->tkn) {
                if (WARN_ON_ONCE(cur != cmd)) {
                    cmd_dump(cmd);
                }
                acked = cur;
                continue;
            }
        }
        if (cur->flags & ECRNX_CMD_FLAG_WAIT_PUSH) {
                next = cur;
                break;
        }
    }
    if (!acked) {
        ECRNX_PRINT(KERN_CRIT "Error: acked cmd not found\n");
    } else {
        cmd->flags &= ~ECRNX_CMD_FLAG_WAIT_ACK;
        if (ECRNX_CMD_WAIT_COMPLETE(cmd->flags))
            cmd_complete(cmd_mgr, cmd);
    }
    if (next) {
        struct ecrnx_hw *ecrnx_hw = container_of(cmd_mgr, struct ecrnx_hw, cmd_mgr);
        next->flags &= ~ECRNX_CMD_FLAG_WAIT_PUSH;
        ecrnx_ipc_msg_push(ecrnx_hw, next, ECRNX_CMD_A2EMSG_LEN(next->a2e_msg));
        kfree(next->a2e_msg);
    }
    spin_unlock(&cmd_mgr->lock);

    return 0;
}



static int cmd_mgr_run_callback(struct ecrnx_hw *ecrnx_hw, struct ecrnx_cmd *cmd,
                                struct ecrnx_cmd_e2amsg *msg, msg_cb_fct cb)
{
    int res;

    if (! cb)
        return 0;

    spin_lock(&ecrnx_hw->cb_lock);
    res = cb(ecrnx_hw, cmd, msg);
    spin_unlock(&ecrnx_hw->cb_lock);

    return res;
}

/**
 *

 */
static int cmd_mgr_msgind(struct ecrnx_cmd_mgr *cmd_mgr, struct ecrnx_cmd_e2amsg *msg,
                          msg_cb_fct cb)
{
    struct ecrnx_hw *ecrnx_hw = container_of(cmd_mgr, struct ecrnx_hw, cmd_mgr);
    struct ecrnx_cmd *cmd;
    bool found = false;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);
    trace_msg_recv(msg->id);

    spin_lock(&cmd_mgr->lock);
    list_for_each_entry(cmd, &cmd_mgr->cmds, list) {
        if (cmd->reqid == msg->id &&
            (cmd->flags & ECRNX_CMD_FLAG_WAIT_CFM)) {

            if (!cmd_mgr_run_callback(ecrnx_hw, cmd, msg, cb)) {
                found = true;
                cmd->flags &= ~ECRNX_CMD_FLAG_WAIT_CFM;

                if (WARN((msg->param_len > ECRNX_CMD_E2AMSG_LEN_MAX),
                         "Unexpect E2A msg len %d > %d\n", msg->param_len,
                         ECRNX_CMD_E2AMSG_LEN_MAX)) {
                    msg->param_len = ECRNX_CMD_E2AMSG_LEN_MAX;
                }

                if (cmd->e2a_msg && msg->param_len)
                    memcpy(cmd->e2a_msg, &msg->param, msg->param_len);

                if (ECRNX_CMD_WAIT_COMPLETE(cmd->flags))
                    cmd_complete(cmd_mgr, cmd);

                break;
            }
        }
    }
    spin_unlock(&cmd_mgr->lock);

    if (!found)
        cmd_mgr_run_callback(ecrnx_hw, NULL, msg, cb);

    ECRNX_DBG("%s exit!! \n", __func__);
    return 0;
}

/**
 *
 */
static void cmd_mgr_print(struct ecrnx_cmd_mgr *cmd_mgr)
{
    struct ecrnx_cmd *cur;

    spin_lock_bh(&cmd_mgr->lock);
    ECRNX_PRINT("q_sz/max: %2d / %2d - next tkn: %d\n",
             cmd_mgr->queue_sz, cmd_mgr->max_queue_sz,
             cmd_mgr->next_tkn);
    list_for_each_entry(cur, &cmd_mgr->cmds, list) {
        cmd_dump(cur);
    }
    spin_unlock_bh(&cmd_mgr->lock);
}

/**
 *
 */
static void cmd_mgr_drain(struct ecrnx_cmd_mgr *cmd_mgr)
{
    struct ecrnx_cmd *cur, *nxt;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    spin_lock_bh(&cmd_mgr->lock);
    list_for_each_entry_safe(cur, nxt, &cmd_mgr->cmds, list) {
        list_del(&cur->list);
        cmd_mgr->queue_sz--;
        if (!(cur->flags & ECRNX_CMD_FLAG_NONBLOCK))
            complete(&cur->complete);
    }
    spin_unlock_bh(&cmd_mgr->lock);
}

/**
 *
 */
void ecrnx_cmd_mgr_init(struct ecrnx_cmd_mgr *cmd_mgr)
{
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    INIT_LIST_HEAD(&cmd_mgr->cmds);
    spin_lock_init(&cmd_mgr->lock);
    cmd_mgr->max_queue_sz = ECRNX_CMD_MAX_QUEUED;
    cmd_mgr->queue  = &cmd_mgr_queue;
    cmd_mgr->print  = &cmd_mgr_print;
    cmd_mgr->drain  = &cmd_mgr_drain;
    cmd_mgr->llind  = &cmd_mgr_llind;
    cmd_mgr->msgind = &cmd_mgr_msgind;
}

/**
 *
 */
void ecrnx_cmd_mgr_deinit(struct ecrnx_cmd_mgr *cmd_mgr)
{
    cmd_mgr->print(cmd_mgr);
    cmd_mgr->drain(cmd_mgr);
    memset(cmd_mgr, 0, sizeof(*cmd_mgr));
}
