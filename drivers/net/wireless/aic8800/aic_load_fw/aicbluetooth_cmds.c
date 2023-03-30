// SPDX-License-Identifier: GPL-2.0-or-later
/**
 ******************************************************************************
 *
 * rwnx_cmds.c
 *
 * Handles queueing (push to IPC, ack/cfm from IPC) of commands issued to
 * LMAC FW
 *
 * Copyright (C) RivieraWaves 2014-2019
 *
 ******************************************************************************
 */

#include <linux/list.h>
#include <stddef.h>
#include "aicbluetooth_cmds.h"
#include "aic_txrxif.h"
#include "aicwf_usb.h"

//extern int aicwf_sdio_writeb(struct aic_sdio_dev *sdiodev, uint regaddr, u8 val);

static void cmd_dump(const struct rwnx_cmd *cmd)
{
    printk(KERN_CRIT "tkn[%d]  flags:%04x  result:%3d  cmd:%4d - reqcfm(%4d)\n",
           cmd->tkn, cmd->flags, cmd->result, cmd->id, cmd->reqid);
}

static void cmd_complete(struct rwnx_cmd_mgr *cmd_mgr, struct rwnx_cmd *cmd)
{
    //printk("cmdcmp\n");
    lockdep_assert_held(&cmd_mgr->lock);

    list_del(&cmd->list);
    cmd_mgr->queue_sz--;

    cmd->flags |= RWNX_CMD_FLAG_DONE;
    if (cmd->flags & RWNX_CMD_FLAG_NONBLOCK) {
        kfree(cmd);
    } else {
        if (RWNX_CMD_WAIT_COMPLETE(cmd->flags)) {
            cmd->result = 0;
            complete(&cmd->complete);
        }
    }
}

static int cmd_mgr_queue(struct rwnx_cmd_mgr *cmd_mgr, struct rwnx_cmd *cmd)
{
    bool defer_push = false;

    spin_lock_bh(&cmd_mgr->lock);

    if (cmd_mgr->state == RWNX_CMD_MGR_STATE_CRASHED) {
        printk(KERN_CRIT"cmd queue crashed\n");
        cmd->result = -EPIPE;
        spin_unlock_bh(&cmd_mgr->lock);
        return -EPIPE;
    }

    if (!list_empty(&cmd_mgr->cmds)) {
        struct rwnx_cmd *last;

        if (cmd_mgr->queue_sz == cmd_mgr->max_queue_sz) {
            printk(KERN_CRIT"Too many cmds (%d) already queued\n",
                   cmd_mgr->max_queue_sz);
            cmd->result = -ENOMEM;
            spin_unlock_bh(&cmd_mgr->lock);
            return -ENOMEM;
        }
        last = list_entry(cmd_mgr->cmds.prev, struct rwnx_cmd, list);
        if (last->flags & (RWNX_CMD_FLAG_WAIT_ACK | RWNX_CMD_FLAG_WAIT_PUSH)) {
            cmd->flags |= RWNX_CMD_FLAG_WAIT_PUSH;
            defer_push = true;
        }
    }

    if (cmd->flags & RWNX_CMD_FLAG_REQ_CFM)
        cmd->flags |= RWNX_CMD_FLAG_WAIT_CFM;

    cmd->tkn    = cmd_mgr->next_tkn++;
    cmd->result = -EINTR;

    if (!(cmd->flags & RWNX_CMD_FLAG_NONBLOCK))
        init_completion(&cmd->complete);

    list_add_tail(&cmd->list, &cmd_mgr->cmds);
    cmd_mgr->queue_sz++;
    spin_unlock_bh(&cmd_mgr->lock);

    if (!defer_push) {
        //printk("queue:id=%x, param_len=%u\n",cmd->a2e_msg->id, cmd->a2e_msg->param_len);
        aicwf_set_cmd_tx((void *)(cmd_mgr->usbdev), cmd->a2e_msg, sizeof(struct lmac_msg) + cmd->a2e_msg->param_len);
        kfree(cmd->a2e_msg);
    } else {
        printk("ERR: never defer push!!!!");
        return 0;
    }

    if (!(cmd->flags & RWNX_CMD_FLAG_NONBLOCK)) {
        unsigned long tout = msecs_to_jiffies(RWNX_80211_CMD_TIMEOUT_MS * cmd_mgr->queue_sz);
        if (!wait_for_completion_killable_timeout(&cmd->complete, tout)) {
            printk(KERN_CRIT"cmd timed-out\n");

            cmd_dump(cmd);
            spin_lock_bh(&cmd_mgr->lock);
            cmd_mgr->state = RWNX_CMD_MGR_STATE_CRASHED;
            if (!(cmd->flags & RWNX_CMD_FLAG_DONE)) {
                cmd->result = -ETIMEDOUT;
                cmd_complete(cmd_mgr, cmd);
            }
            spin_unlock_bh(&cmd_mgr->lock);
        }
        else{
            kfree(cmd);
        }
    } else {
        cmd->result = 0;
    }

    return 0;
}

static int cmd_mgr_run_callback(struct rwnx_cmd_mgr *cmd_mgr, struct rwnx_cmd *cmd,
                                struct rwnx_cmd_e2amsg *msg, msg_cb_fct cb)
{
    int res;

    if (! cb){
        return 0;
    }
    spin_lock(&cmd_mgr->cb_lock);
    res = cb(cmd, msg);
    spin_unlock(&cmd_mgr->cb_lock);

    return res;
}

static int cmd_mgr_msgind(struct rwnx_cmd_mgr *cmd_mgr, struct rwnx_cmd_e2amsg *msg,
                          msg_cb_fct cb)
{
    struct rwnx_cmd *cmd;
    bool found = false;

    //printk("cmd->id=%x\n", msg->id);
    spin_lock(&cmd_mgr->lock);
    list_for_each_entry(cmd, &cmd_mgr->cmds, list) {
        if (cmd->reqid == msg->id &&
            (cmd->flags & RWNX_CMD_FLAG_WAIT_CFM)) {

            if (!cmd_mgr_run_callback(cmd_mgr, cmd, msg, cb)) {
                found = true;
                cmd->flags &= ~RWNX_CMD_FLAG_WAIT_CFM;

                if (WARN((msg->param_len > RWNX_CMD_E2AMSG_LEN_MAX),
                         "Unexpect E2A msg len %d > %d\n", msg->param_len,
                         RWNX_CMD_E2AMSG_LEN_MAX)) {
                    msg->param_len = RWNX_CMD_E2AMSG_LEN_MAX;
                }

                if (cmd->e2a_msg && msg->param_len)
                    memcpy(cmd->e2a_msg, &msg->param, msg->param_len);

                if (RWNX_CMD_WAIT_COMPLETE(cmd->flags))
                    cmd_complete(cmd_mgr, cmd);

                break;
            }
        }
    }
    spin_unlock(&cmd_mgr->lock);

    if (!found)
        cmd_mgr_run_callback(cmd_mgr, NULL, msg, cb);

    return 0;
}

static void cmd_mgr_print(struct rwnx_cmd_mgr *cmd_mgr)
{
    struct rwnx_cmd *cur;

    spin_lock_bh(&cmd_mgr->lock);
    printk("q_sz/max: %2d / %2d - next tkn: %d\n",
             cmd_mgr->queue_sz, cmd_mgr->max_queue_sz,
             cmd_mgr->next_tkn);
    list_for_each_entry(cur, &cmd_mgr->cmds, list) {
        cmd_dump(cur);
    }
    spin_unlock_bh(&cmd_mgr->lock);
}

static void cmd_mgr_drain(struct rwnx_cmd_mgr *cmd_mgr)
{
    struct rwnx_cmd *cur, *nxt;

    spin_lock_bh(&cmd_mgr->lock);
    list_for_each_entry_safe(cur, nxt, &cmd_mgr->cmds, list) {
        list_del(&cur->list);
        cmd_mgr->queue_sz--;
        if (!(cur->flags & RWNX_CMD_FLAG_NONBLOCK))
            complete(&cur->complete);
    }
    spin_unlock_bh(&cmd_mgr->lock);
}

void rwnx_cmd_mgr_init(struct rwnx_cmd_mgr *cmd_mgr)
{
    cmd_mgr->max_queue_sz = RWNX_CMD_MAX_QUEUED;
    INIT_LIST_HEAD(&cmd_mgr->cmds);
    cmd_mgr->state = RWNX_CMD_MGR_STATE_INITED;
    spin_lock_init(&cmd_mgr->lock);
    spin_lock_init(&cmd_mgr->cb_lock);
    cmd_mgr->queue  = &cmd_mgr_queue;
    cmd_mgr->print  = &cmd_mgr_print;
    cmd_mgr->drain  = &cmd_mgr_drain;
    cmd_mgr->llind  = NULL;//&cmd_mgr_llind;
    cmd_mgr->msgind = &cmd_mgr_msgind;

    #if 0
    INIT_WORK(&cmd_mgr->cmdWork, cmd_mgr_task_process);
    cmd_mgr->cmd_wq = create_singlethread_workqueue("cmd_wq");
    if (!cmd_mgr->cmd_wq) {
        txrx_err("insufficient memory to create cmd workqueue.\n");
        return;
    }
    #endif
}

void rwnx_cmd_mgr_deinit(struct rwnx_cmd_mgr *cmd_mgr)
{
    cmd_mgr->print(cmd_mgr);
    cmd_mgr->drain(cmd_mgr);
    cmd_mgr->print(cmd_mgr);
    memset(cmd_mgr, 0, sizeof(*cmd_mgr));
}

void aicwf_set_cmd_tx(void *dev, struct lmac_msg *msg, uint len)
{
	struct aic_usb_dev *usbdev = (struct aic_usb_dev *)dev;
    struct aicwf_bus *bus = usbdev->bus_if;
    u8 *buffer = bus->cmd_buf;
    u16 index = 0;

    memset(buffer, 0, CMD_BUF_MAX);
    buffer[0] = (len+4) & 0x00ff;
    buffer[1] = ((len+4) >> 8) &0x0f;
    buffer[2] = 0x11;
    buffer[3] = 0x0;
    index += 4;
    //there is a dummy word
    index += 4;

	//make sure little endian
    put_u16(&buffer[index], msg->id);
    index += 2;
    put_u16(&buffer[index], msg->dest_id);
    index += 2;
    put_u16(&buffer[index], msg->src_id);
    index += 2;
    put_u16(&buffer[index], msg->param_len);
    index += 2;
    memcpy(&buffer[index], (u8 *)msg->param, msg->param_len);

    aicwf_bus_txmsg(bus, buffer, len + 8);
}

static inline void *rwnx_msg_zalloc(lmac_msg_id_t const id,
                                    lmac_task_id_t const dest_id,
                                    lmac_task_id_t const src_id,
                                    uint16_t const param_len)
{
    struct lmac_msg *msg;
    gfp_t flags;

    if (in_softirq())
        flags = GFP_ATOMIC;
    else
        flags = GFP_KERNEL;

    msg = (struct lmac_msg *)kzalloc(sizeof(struct lmac_msg) + param_len,
                                     flags);
    if (msg == NULL) {
        printk(KERN_CRIT "%s: msg allocation failed\n", __func__);
        return NULL;
    }
    msg->id = id;
    msg->dest_id = dest_id;
    msg->src_id = src_id;
    msg->param_len = param_len;

    return msg->param;
}

static void rwnx_msg_free(struct lmac_msg *msg, const void *msg_params)
{
    kfree(msg);
}


static int rwnx_send_msg(struct aic_usb_dev *usbdev, const void *msg_params,
                         int reqcfm, lmac_msg_id_t reqid, void *cfm)
{
    struct lmac_msg *msg;
    struct rwnx_cmd *cmd;
    bool nonblock;
    int ret = 0;

    msg = container_of((void *)msg_params, struct lmac_msg, param);
    if(usbdev->bus_if->state == BUS_DOWN_ST) {
        rwnx_msg_free(msg, msg_params);
        printk("bus is down\n");
        return 0;
    }

    nonblock = 0;
    cmd = kzalloc(sizeof(struct rwnx_cmd), nonblock ? GFP_ATOMIC : GFP_KERNEL);
    cmd->result  = -EINTR;
    cmd->id      = msg->id;
    cmd->reqid   = reqid;
    cmd->a2e_msg = msg;
    cmd->e2a_msg = cfm;
    if (nonblock)
        cmd->flags = RWNX_CMD_FLAG_NONBLOCK;
    if (reqcfm)
        cmd->flags |= RWNX_CMD_FLAG_REQ_CFM;

    if(reqcfm) {
        cmd->flags &= ~RWNX_CMD_FLAG_WAIT_ACK; // we don't need ack any more
        ret = usbdev->cmd_mgr.queue(&usbdev->cmd_mgr,cmd);
    } else {
        aicwf_set_cmd_tx((void *)(usbdev), cmd->a2e_msg, sizeof(struct lmac_msg) + cmd->a2e_msg->param_len);
    }

    if(!reqcfm)
        kfree(cmd);

    return ret;
}

int rwnx_send_dbg_mem_mask_write_req(struct aic_usb_dev *usbdev, u32 mem_addr,
                                     u32 mem_mask, u32 mem_data)
{
    struct dbg_mem_mask_write_req *mem_mask_write_req;

    /* Build the DBG_MEM_MASK_WRITE_REQ message */
    mem_mask_write_req = rwnx_msg_zalloc(DBG_MEM_MASK_WRITE_REQ, TASK_DBG, DRV_TASK_ID,
                                         sizeof(struct dbg_mem_mask_write_req));
    if (!mem_mask_write_req)
        return -ENOMEM;

    /* Set parameters for the DBG_MEM_MASK_WRITE_REQ message */
    mem_mask_write_req->memaddr = mem_addr;
    mem_mask_write_req->memmask = mem_mask;
    mem_mask_write_req->memdata = mem_data;

    /* Send the DBG_MEM_MASK_WRITE_REQ message to LMAC FW */
    return rwnx_send_msg(usbdev, mem_mask_write_req, 1, DBG_MEM_MASK_WRITE_CFM, NULL);
}



int rwnx_send_dbg_mem_block_write_req(struct aic_usb_dev *usbdev, u32 mem_addr,
                                      u32 mem_size, u32 *mem_data)
{
    struct dbg_mem_block_write_req *mem_blk_write_req;

    /* Build the DBG_MEM_BLOCK_WRITE_REQ message */
    mem_blk_write_req = rwnx_msg_zalloc(DBG_MEM_BLOCK_WRITE_REQ, TASK_DBG, DRV_TASK_ID,
                                        sizeof(struct dbg_mem_block_write_req));
    if (!mem_blk_write_req)
        return -ENOMEM;

    /* Set parameters for the DBG_MEM_BLOCK_WRITE_REQ message */
    mem_blk_write_req->memaddr = mem_addr;
    mem_blk_write_req->memsize = mem_size;
    memcpy(mem_blk_write_req->memdata, mem_data, mem_size);

    /* Send the DBG_MEM_BLOCK_WRITE_REQ message to LMAC FW */
    return rwnx_send_msg(usbdev, mem_blk_write_req, 1, DBG_MEM_BLOCK_WRITE_CFM, NULL);
}


int rwnx_send_dbg_mem_read_req(struct aic_usb_dev *usbdev, u32 mem_addr,
                               struct dbg_mem_read_cfm *cfm)
{
    struct dbg_mem_read_req *mem_read_req;


    /* Build the DBG_MEM_READ_REQ message */
    mem_read_req = rwnx_msg_zalloc(DBG_MEM_READ_REQ, TASK_DBG, DRV_TASK_ID,
                                   sizeof(struct dbg_mem_read_req));
    if (!mem_read_req)
        return -ENOMEM;

    /* Set parameters for the DBG_MEM_READ_REQ message */
    mem_read_req->memaddr = mem_addr;

    /* Send the DBG_MEM_READ_REQ message to LMAC FW */
    return rwnx_send_msg(usbdev, mem_read_req, 1, DBG_MEM_READ_CFM, cfm);
}


int rwnx_send_dbg_mem_write_req(struct aic_usb_dev *usbdev, u32 mem_addr, u32 mem_data)
{
    struct dbg_mem_write_req *mem_write_req;

	//printk("%s mem_addr:%x mem_data:%x\r\n", __func__, mem_addr, mem_data);

    /* Build the DBG_MEM_WRITE_REQ message */
    mem_write_req = rwnx_msg_zalloc(DBG_MEM_WRITE_REQ, TASK_DBG, DRV_TASK_ID,
                                    sizeof(struct dbg_mem_write_req));
    if (!mem_write_req)
        return -ENOMEM;

    /* Set parameters for the DBG_MEM_WRITE_REQ message */
    mem_write_req->memaddr = mem_addr;
    mem_write_req->memdata = mem_data;

    /* Send the DBG_MEM_WRITE_REQ message to LMAC FW */
    return rwnx_send_msg(usbdev, mem_write_req, 1, DBG_MEM_WRITE_CFM, NULL);
}

int rwnx_send_dbg_start_app_req(struct aic_usb_dev *usbdev, u32 boot_addr,
                                u32 boot_type)
{
    struct dbg_start_app_req *start_app_req;


    /* Build the DBG_START_APP_REQ message */
    start_app_req = rwnx_msg_zalloc(DBG_START_APP_REQ, TASK_DBG, DRV_TASK_ID,
                                    sizeof(struct dbg_start_app_req));
    if (!start_app_req)
        return -ENOMEM;

    /* Set parameters for the DBG_START_APP_REQ message */
    start_app_req->bootaddr = boot_addr;
    start_app_req->boottype = boot_type;

    /* Send the DBG_START_APP_REQ message to LMAC FW */
    return rwnx_send_msg(usbdev, start_app_req, 0, 0, NULL);
}

static msg_cb_fct dbg_hdlrs[MSG_I(DBG_MAX)] = {
};

static msg_cb_fct *msg_hdlrs[] = {
    [TASK_DBG]   = dbg_hdlrs,
};

void rwnx_rx_handle_msg(struct aic_usb_dev *usbdev, struct ipc_e2a_msg *msg)
{
    usbdev->cmd_mgr.msgind(&usbdev->cmd_mgr, msg,
                            msg_hdlrs[MSG_T(msg->id)][MSG_I(msg->id)]);
}


int rwnx_send_reboot(struct aic_usb_dev *usbdev)
{
    int ret = 0;
    u32 delay = 2 *1000; //1s

    printk("%s enter \r\n", __func__);

    ret = rwnx_send_dbg_start_app_req(usbdev, delay, HOST_START_APP_REBOOT);
    return ret;
}



