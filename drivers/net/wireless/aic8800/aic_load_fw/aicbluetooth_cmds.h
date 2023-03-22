/**
 ******************************************************************************
 *
 * rwnx_cmds.h
 *
 * Copyright (C) RivieraWaves 2014-2019
 *
 ******************************************************************************
 */

#ifndef _AICBLUETOOTH_CMDS_H_
#define _AICBLUETOOTH_CMDS_H_

#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/module.h>

#define RWNX_80211_CMD_TIMEOUT_MS    2000//500//300

#define RWNX_CMD_FLAG_NONBLOCK      BIT(0)
#define RWNX_CMD_FLAG_REQ_CFM       BIT(1)
#define RWNX_CMD_FLAG_WAIT_PUSH     BIT(2)
#define RWNX_CMD_FLAG_WAIT_ACK      BIT(3)
#define RWNX_CMD_FLAG_WAIT_CFM      BIT(4)
#define RWNX_CMD_FLAG_DONE          BIT(5)
/* ATM IPC design makes it possible to get the CFM before the ACK,
 * otherwise this could have simply been a state enum */
#define RWNX_CMD_WAIT_COMPLETE(flags) \
    (!(flags & (RWNX_CMD_FLAG_WAIT_ACK | RWNX_CMD_FLAG_WAIT_CFM)))

#define RWNX_CMD_MAX_QUEUED         8

//#include "ipc_shared.h"

#define IPC_E2A_MSG_PARAM_SIZE 256

/// Message structure for MSGs from Emb to App
struct ipc_e2a_msg
{
    u16 id;                ///< Message id.
    u16 dummy_dest_id;
    u16 dummy_src_id;
    u16 param_len;         ///< Parameter embedded struct length.    
    u32 pattern;           ///< Used to stamp a valid MSG buffer
    u32 param[IPC_E2A_MSG_PARAM_SIZE];  ///< Parameter embedded struct. Must be word-aligned.
};

typedef u16 lmac_msg_id_t;
typedef u16 lmac_task_id_t;

struct lmac_msg
{
    lmac_msg_id_t     id;         ///< Message id.
    lmac_task_id_t    dest_id;    ///< Destination kernel identifier.
    lmac_task_id_t    src_id;     ///< Source kernel identifier.
    u16        param_len;  ///< Parameter embedded struct length.
    u32        param[];   ///< Parameter embedded struct. Must be word-aligned.
};

#define rwnx_cmd_e2amsg ipc_e2a_msg
#define rwnx_cmd_a2emsg lmac_msg
#define RWNX_CMD_A2EMSG_LEN(m) (sizeof(struct lmac_msg) + m->param_len)
#define RWNX_CMD_E2AMSG_LEN_MAX (IPC_E2A_MSG_PARAM_SIZE * 4)

static inline void put_u16(u8 *buf, u16 data)
{
    buf[0] = (u8)(data&0x00ff);
    buf[1] = (u8)((data >> 8)&0x00ff);
}

enum rwnx_cmd_mgr_state {
    RWNX_CMD_MGR_STATE_DEINIT,
    RWNX_CMD_MGR_STATE_INITED,
    RWNX_CMD_MGR_STATE_CRASHED,
};

struct rwnx_cmd {
    struct list_head list;
    lmac_msg_id_t id;
    lmac_msg_id_t reqid;
    struct rwnx_cmd_a2emsg *a2e_msg;
    char *e2a_msg;
    u32 tkn;
    u16 flags;
    struct completion complete;
    u32 result;
};

struct aic_sdio_dev;
struct aic_usb_dev;
struct rwnx_cmd;
typedef int (*msg_cb_fct)(struct rwnx_cmd *cmd, struct rwnx_cmd_e2amsg *msg);

struct rwnx_cmd_mgr {
    enum rwnx_cmd_mgr_state state;
    spinlock_t lock;
    u32 next_tkn;
    u32 queue_sz;
    u32 max_queue_sz;
    spinlock_t cb_lock;
    #if 0
    void *sdiodev;
    #else
    void *usbdev;
    #endif
    struct list_head cmds;

    int  (*queue)(struct rwnx_cmd_mgr *, struct rwnx_cmd *);
    int  (*llind)(struct rwnx_cmd_mgr *, struct rwnx_cmd *);
    int  (*msgind)(struct rwnx_cmd_mgr *, struct rwnx_cmd_e2amsg *, msg_cb_fct);
    void (*print)(struct rwnx_cmd_mgr *);
    void (*drain)(struct rwnx_cmd_mgr *);

    struct work_struct cmdWork;
    struct workqueue_struct *cmd_wq;
};


#if 0
#define WAKE_CMD_WORK(cmd_mgr) \
    do { \
        queue_work((cmd_mgr)->cmd_wq, &cmd_mgr->cmdWork); \
    } while (0)
#endif
void rwnx_cmd_mgr_init(struct rwnx_cmd_mgr *cmd_mgr);
void rwnx_cmd_mgr_deinit(struct rwnx_cmd_mgr *cmd_mgr);
int cmd_mgr_queue_force_defer(struct rwnx_cmd_mgr *cmd_mgr, struct rwnx_cmd *cmd);
void aicwf_set_cmd_tx(void *dev, struct lmac_msg *msg, uint len);

enum
{
    TASK_NONE = (u8) -1,

    // MAC Management task.
    TASK_MM = 0,
    // DEBUG task
    TASK_DBG,
    /// SCAN task
    TASK_SCAN,
    /// TDLS task
    TASK_TDLS,
    /// SCANU task
    TASK_SCANU,
    /// ME task
    TASK_ME,
    /// SM task
    TASK_SM,
    /// APM task
    TASK_APM,
    /// BAM task
    TASK_BAM,
    /// MESH task
    TASK_MESH,
    /// RXU task
    TASK_RXU,
    // This is used to define the last task that is running on the EMB processor
    TASK_LAST_EMB = TASK_RXU,

    // nX API task
    TASK_API,
    TASK_MAX,
};

#define LMAC_FIRST_MSG(task) ((lmac_msg_id_t)((task) << 10))
#define DRV_TASK_ID 100
#define MSG_I(msg) ((msg) & ((1<<10)-1))
#define MSG_T(msg) ((lmac_task_id_t)((msg) >> 10))


enum dbg_msg_tag
{
    /// Memory read request
    DBG_MEM_READ_REQ = LMAC_FIRST_MSG(TASK_DBG),
    /// Memory read confirm
    DBG_MEM_READ_CFM,
    /// Memory write request
    DBG_MEM_WRITE_REQ,
    /// Memory write confirm
    DBG_MEM_WRITE_CFM,
    /// Module filter request
    DBG_SET_MOD_FILTER_REQ,
    /// Module filter confirm
    DBG_SET_MOD_FILTER_CFM,
    /// Severity filter request
    DBG_SET_SEV_FILTER_REQ,
    /// Severity filter confirm
    DBG_SET_SEV_FILTER_CFM,
    /// LMAC/MAC HW fatal error indication
    DBG_ERROR_IND,
    /// Request to get system statistics
    DBG_GET_SYS_STAT_REQ,
    /// COnfirmation of system statistics
    DBG_GET_SYS_STAT_CFM,
    /// Memory block write request
    DBG_MEM_BLOCK_WRITE_REQ,
    /// Memory block write confirm
    DBG_MEM_BLOCK_WRITE_CFM,
    /// Start app request
    DBG_START_APP_REQ,
    /// Start app confirm
    DBG_START_APP_CFM,
    /// Start npc request
    DBG_START_NPC_REQ,
    /// Start npc confirm
    DBG_START_NPC_CFM,
    /// Memory mask write request
    DBG_MEM_MASK_WRITE_REQ,
    /// Memory mask write confirm
    DBG_MEM_MASK_WRITE_CFM,
    /// Max number of Debug messages
    DBG_MAX,
};

struct dbg_mem_block_write_req
{
    u32 memaddr;
    u32 memsize;
    u32 memdata[1024 / sizeof(u32)];
};

/// Structure containing the parameters of the @ref DBG_MEM_MASK_WRITE_REQ message.
struct dbg_mem_mask_write_req
{
    u32 memaddr;
    u32 memmask;
    u32 memdata;
};


/// Structure containing the parameters of the @ref DBG_MEM_BLOCK_WRITE_CFM message.
struct dbg_mem_block_write_cfm
{
    u32 wstatus;
};

/// Structure containing the parameters of the @ref DBG_MEM_READ_REQ message.
struct dbg_mem_read_req
{
    u32 memaddr;
};

/// Structure containing the parameters of the @ref DBG_MEM_READ_CFM message.
struct dbg_mem_read_cfm
{
    u32 memaddr;
    u32 memdata;
};

/// Structure containing the parameters of the @ref DBG_MEM_WRITE_REQ message.
struct dbg_mem_write_req
{
    u32 memaddr;
    u32 memdata;
};

/// Structure containing the parameters of the @ref DBG_MEM_WRITE_CFM message.
struct dbg_mem_write_cfm
{
    u32 memaddr;
    u32 memdata;
};

struct dbg_start_app_req
{
    u32 bootaddr;
    u32 boottype;
};

/// Structure containing the parameters of the @ref DBG_START_APP_CFM message.
struct dbg_start_app_cfm
{
    u32 bootstatus;
};

enum {
    HOST_START_APP_AUTO = 1,
    HOST_START_APP_CUSTOM,
    HOST_START_APP_REBOOT,
};

int rwnx_send_dbg_mem_mask_write_req(struct aic_usb_dev *usbdev, u32 mem_addr,
                                     u32 mem_mask, u32 mem_data);


int rwnx_send_dbg_mem_block_write_req(struct aic_usb_dev *usbdev, u32 mem_addr,
                                      u32 mem_size, u32 *mem_data);
                                      
int rwnx_send_dbg_mem_write_req(struct aic_usb_dev *usbdev, u32 mem_addr, u32 mem_data);
int rwnx_send_dbg_mem_read_req(struct aic_usb_dev *usbdev, u32 mem_addr, struct dbg_mem_read_cfm *cfm);

void rwnx_rx_handle_msg(struct aic_usb_dev *usbdev, struct ipc_e2a_msg *msg);

int rwnx_send_dbg_start_app_req(struct aic_usb_dev *usbdev, u32 boot_addr,
                                u32 boot_type);

int rwnx_send_reboot(struct aic_usb_dev *usbdev);

#endif
