/**
 ******************************************************************************
 *
 * ecrnx_cmds.h
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#ifndef _ECRNX_CMDS_H_
#define _ECRNX_CMDS_H_

#include <linux/spinlock.h>
#include <linux/completion.h>
#include "lmac_msg.h"

#ifdef CONFIG_ECRNX_SDM
#define ECRNX_80211_CMD_TIMEOUT_MS    (20 * 300)
#elif defined(CONFIG_ECRNX_FHOST)
#define ECRNX_80211_CMD_TIMEOUT_MS    (10000)
#else
#define ECRNX_80211_CMD_TIMEOUT_MS    (20 * 300) //300
#endif

#define ECRNX_CMD_FLAG_NONBLOCK      BIT(0)
#define ECRNX_CMD_FLAG_REQ_CFM       BIT(1)
#define ECRNX_CMD_FLAG_WAIT_PUSH     BIT(2)
#define ECRNX_CMD_FLAG_WAIT_ACK      BIT(3)
#define ECRNX_CMD_FLAG_WAIT_CFM      BIT(4)
#define ECRNX_CMD_FLAG_DONE          BIT(5)
/* ATM IPC design makes it possible to get the CFM before the ACK,
 * otherwise this could have simply been a state enum */
#define ECRNX_CMD_WAIT_COMPLETE(flags) \
    (!(flags & (ECRNX_CMD_FLAG_WAIT_ACK | ECRNX_CMD_FLAG_WAIT_CFM)))

#define ECRNX_CMD_MAX_QUEUED         8

#ifdef CONFIG_ECRNX_FHOST
#include "ipc_fhost.h"
#define ecrnx_cmd_e2amsg ipc_fhost_msg
#define ecrnx_cmd_a2emsg ipc_fhost_msg
#define ECRNX_CMD_A2EMSG_LEN(m) (m->param_len)
#define ECRNX_CMD_E2AMSG_LEN_MAX IPC_FHOST_MSG_BUF_SIZE
struct ecrnx_term_stream;

#else /* !CONFIG_ECRNX_FHOST*/
#include "ipc_shared.h"
#define ecrnx_cmd_e2amsg ipc_e2a_msg
#define ecrnx_cmd_a2emsg lmac_msg
#define ECRNX_CMD_A2EMSG_LEN(m) (sizeof(struct lmac_msg) + m->param_len)
#define ECRNX_CMD_E2AMSG_LEN_MAX (IPC_E2A_MSG_PARAM_SIZE * 4)

#endif /* CONFIG_ECRNX_FHOST*/

struct ecrnx_hw;
struct ecrnx_cmd;
typedef int (*msg_cb_fct)(struct ecrnx_hw *ecrnx_hw, struct ecrnx_cmd *cmd,
                          struct ecrnx_cmd_e2amsg *msg);

enum ecrnx_cmd_mgr_state {
    ECRNX_CMD_MGR_STATE_DEINIT,
    ECRNX_CMD_MGR_STATE_INITED,
    ECRNX_CMD_MGR_STATE_CRASHED,
};

struct ecrnx_cmd {
    struct list_head list;
    lmac_msg_id_t id;
    lmac_msg_id_t reqid;
    struct ecrnx_cmd_a2emsg *a2e_msg;
    char *e2a_msg;
    u32 tkn;
    u16 flags;

    struct completion complete;
    u32 result;
    #ifdef CONFIG_ECRNX_FHOST
    struct ecrnx_term_stream *stream;
    #endif
};

struct ecrnx_cmd_mgr {
    enum ecrnx_cmd_mgr_state state;
    spinlock_t lock;
    u32 next_tkn;
    u32 queue_sz;
    u32 max_queue_sz;

    struct list_head cmds;

    int  (*queue)(struct ecrnx_cmd_mgr *, struct ecrnx_cmd *);
    int  (*llind)(struct ecrnx_cmd_mgr *, struct ecrnx_cmd *);
    int  (*msgind)(struct ecrnx_cmd_mgr *, struct ecrnx_cmd_e2amsg *, msg_cb_fct);
    void (*print)(struct ecrnx_cmd_mgr *);
    void (*drain)(struct ecrnx_cmd_mgr *);
};

void ecrnx_cmd_mgr_init(struct ecrnx_cmd_mgr *cmd_mgr);
void ecrnx_cmd_mgr_deinit(struct ecrnx_cmd_mgr *cmd_mgr);

#endif /* _ECRNX_CMDS_H_ */
