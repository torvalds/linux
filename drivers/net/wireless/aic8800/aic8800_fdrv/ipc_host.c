/**
 ******************************************************************************
 *
 * @file ipc_host.c
 *
 * @brief IPC module.
 *
 * Copyright (C) RivieraWaves 2011-2019
 *
 ******************************************************************************
 */

/*
 * INCLUDE FILES
 ******************************************************************************
 */
#ifndef __KERNEL__
#include <stdio.h>
#define REG_SW_SET_PROFILING(env, value)   do{  }while(0)
#define REG_SW_CLEAR_PROFILING(env, value)   do{  }while(0)
#define REG_SW_CLEAR_HOSTBUF_IDX_PROFILING(env)   do{  }while(0)
#define REG_SW_SET_HOSTBUF_IDX_PROFILING(env, val)   do{  }while(0)
#else
#include <linux/spinlock.h>
#include "rwnx_defs.h"
#include "rwnx_prof.h"
#endif

#include "reg_ipc_app.h"
#include "ipc_host.h"

/*
 * TYPES DEFINITION
 ******************************************************************************
 */

const int nx_txdesc_cnt[] =
{
    NX_TXDESC_CNT0,
    NX_TXDESC_CNT1,
    NX_TXDESC_CNT2,
    NX_TXDESC_CNT3,
    #if NX_TXQ_CNT == 5
    NX_TXDESC_CNT4,
    #endif
};

const int nx_txdesc_cnt_msk[] =
{
    NX_TXDESC_CNT0 - 1,
    NX_TXDESC_CNT1 - 1,
    NX_TXDESC_CNT2 - 1,
    NX_TXDESC_CNT3 - 1,
    #if NX_TXQ_CNT == 5
    NX_TXDESC_CNT4 - 1,
    #endif
};

const int nx_txuser_cnt[] =
{
    CONFIG_USER_MAX,
    CONFIG_USER_MAX,
    CONFIG_USER_MAX,
    CONFIG_USER_MAX,
    #if NX_TXQ_CNT == 5
    1,
    #endif
};


/*
 * FUNCTIONS DEFINITIONS
 ******************************************************************************
 */
/**
 * ipc_host_rxdesc_handler() - Handle the reception of a Rx Descriptor
 *
 * @env: pointer to the IPC Host environment
 *
 * Called from general IRQ handler when status %IPC_IRQ_E2A_RXDESC is set
 */
static void ipc_host_rxdesc_handler(struct ipc_host_env_tag *env)
{
    // For profiling
    REG_SW_SET_PROFILING(env->pthis, SW_PROF_IRQ_E2A_RXDESC);

    // LMAC has triggered an IT saying that a reception has occurred.
    // Then we first need to check the validity of the current hostbuf, and the validity
    // of the next hostbufs too, because it is likely that several hostbufs have been
    // filled within the time needed for this irq handling
    do {
        #ifdef CONFIG_RWNX_FULLMAC
        // call the external function to indicate that a RX descriptor is received
        if (env->cb.recv_data_ind(env->pthis,
                                  env->ipc_host_rxdesc_array[env->ipc_host_rxdesc_idx].hostid) != 0)
        #else
        // call the external function to indicate that a RX packet is received
        if (env->cb.recv_data_ind(env->pthis,
                                  env->ipc_host_rxbuf_array[env->ipc_host_rxbuf_idx].hostid) != 0)
        #endif //(CONFIG_RWNX_FULLMAC)
            break;

    }while(1);

    // For profiling
    REG_SW_CLEAR_PROFILING(env->pthis, SW_PROF_IRQ_E2A_RXDESC);
}

/**
 * ipc_host_radar_handler() - Handle the reception of radar events
 *
 * @env: pointer to the IPC Host environment
 *
 * Called from general IRQ handler when status %IPC_IRQ_E2A_RADAR is set
 */
static void ipc_host_radar_handler(struct ipc_host_env_tag *env)
{
#ifdef CONFIG_RWNX_RADAR
    // LMAC has triggered an IT saying that a radar event has been sent to upper layer.
    // Then we first need to check the validity of the current msg buf, and the validity
    // of the next buffers too, because it is likely that several buffers have been
    // filled within the time needed for this irq handling
    // call the external function to indicate that a RX packet is received
    spin_lock_bh(&((struct rwnx_hw *)env->pthis)->radar.lock);
    while (env->cb.recv_radar_ind(env->pthis,
              env->ipc_host_radarbuf_array[env->ipc_host_radarbuf_idx].hostid) == 0)
        ;
    spin_unlock_bh(&((struct rwnx_hw *)env->pthis)->radar.lock);
#endif /* CONFIG_RWNX_RADAR */
}

/**
 * ipc_host_unsup_rx_vec_handler() - Handle the reception of unsupported rx vector
 *
 * @env: pointer to the IPC Host environment
 *
 * Called from general IRQ handler when status %IPC_IRQ_E2A_UNSUP_RX_VEC is set
 */
static void ipc_host_unsup_rx_vec_handler(struct ipc_host_env_tag *env)
{
    while (env->cb.recv_unsup_rx_vec_ind(env->pthis,
              env->ipc_host_unsuprxvecbuf_array[env->ipc_host_unsuprxvecbuf_idx].hostid) == 0)
        ;
}

/**
 * ipc_host_msg_handler() - Handler for firmware message
 *
 * @env: pointer to the IPC Host environment
 *
 * Called from general IRQ handler when status %IPC_IRQ_E2A_MSG is set
 */
static void ipc_host_msg_handler(struct ipc_host_env_tag *env)
{
    // For profiling
    REG_SW_SET_PROFILING(env->pthis, SW_PROF_IRQ_E2A_MSG);

    // LMAC has triggered an IT saying that a message has been sent to upper layer.
    // Then we first need to check the validity of the current msg buf, and the validity
    // of the next buffers too, because it is likely that several buffers have been
    // filled within the time needed for this irq handling
    // call the external function to indicate that a RX packet is received
    while (env->cb.recv_msg_ind(env->pthis,
                    env->ipc_host_msgbuf_array[env->ipc_host_msge2a_idx].hostid) == 0)
        ;


    // For profiling
    REG_SW_CLEAR_PROFILING(env->pthis, SW_PROF_IRQ_E2A_MSG);

}

/**
 * ipc_host_msgack_handler() - Handle the reception of message acknowledgement
 *
 * @env: pointer to the IPC Host environment
 *
 * Called from general IRQ handler when status %IPC_IRQ_E2A_MSG_ACK is set
 */
static void ipc_host_msgack_handler(struct ipc_host_env_tag *env)
{
    void *hostid = env->msga2e_hostid;

    ASSERT_ERR(hostid);
    ASSERT_ERR(env->msga2e_cnt == (((struct lmac_msg *)(&env->shared->msg_a2e_buf.msg))->src_id & 0xFF));

    env->msga2e_hostid = NULL;
    env->msga2e_cnt++;
    env->cb.recv_msgack_ind(env->pthis, hostid);
}

/**
 * ipc_host_dbg_handler() - Handle the reception of Debug event
 *
 * @env: pointer to the IPC Host environment
 *
 * Called from general IRQ handler when status %IPC_IRQ_E2A_DBG is set
 */
static void ipc_host_dbg_handler(struct ipc_host_env_tag *env)
{
    // For profiling
    REG_SW_SET_PROFILING(env->pthis, SW_PROF_IRQ_E2A_DBG);

    // LMAC has triggered an IT saying that a DBG message has been sent to upper layer.
    // Then we first need to check the validity of the current buffer, and the validity
    // of the next buffers too, because it is likely that several buffers have been
    // filled within the time needed for this irq handling
    // call the external function to indicate that a RX packet is received
    while(env->cb.recv_dbg_ind(env->pthis,
            env->ipc_host_dbgbuf_array[env->ipc_host_dbg_idx].hostid) == 0)
        ;

    // For profiling
    REG_SW_CLEAR_PROFILING(env->pthis, SW_PROF_IRQ_E2A_DBG);
}

/**
 * ipc_host_tx_cfm_handler() - Handle the reception of TX confirmation
 *
 * @env: pointer to the IPC Host environment
 * @queue_idx: index of the hardware on which the confirmation has been received
 * @user_pos: index of the user position
 *
 * Called from general IRQ handler when status %IPC_IRQ_E2A_TXCFM is set
 */
static void ipc_host_tx_cfm_handler(struct ipc_host_env_tag *env,
                                    const int queue_idx, const int user_pos)
{
    // TX confirmation descriptors have been received
    REG_SW_SET_PROFILING(env->pthis, SW_PROF_IRQ_E2A_TXCFM);
    while (1)
    {
        // Get the used index and increase it. We do the increase before knowing if the
        // current buffer is confirmed because the callback function may call the
        // ipc_host_txdesc_get() in case flow control was enabled and the index has to be
        // already at the good value to ensure that the test of FIFO full is correct
        uint32_t used_idx = env->txdesc_used_idx[queue_idx][user_pos]++;
        uint32_t used_idx_mod = used_idx & nx_txdesc_cnt_msk[queue_idx];
        void *host_id = env->tx_host_id[queue_idx][user_pos][used_idx_mod];

        // Reset the host id in the array
        env->tx_host_id[queue_idx][user_pos][used_idx_mod] = 0;

        // call the external function to indicate that a TX packet is freed
        if (host_id == 0)
        {
            // No more confirmations, so put back the used index at its initial value
            env->txdesc_used_idx[queue_idx][user_pos] = used_idx;
            break;
        }

        if (env->cb.send_data_cfm(env->pthis, host_id) != 0)
        {
            // No more confirmations, so put back the used index at its initial value
            env->txdesc_used_idx[queue_idx][user_pos] = used_idx;
            env->tx_host_id[queue_idx][user_pos][used_idx_mod] = host_id;
            // and exit the loop
            break;
        }

        REG_SW_SET_PROFILING_CHAN(env->pthis, SW_PROF_CHAN_CTXT_CFM_HDL_BIT);
        REG_SW_CLEAR_PROFILING_CHAN(env->pthis, SW_PROF_CHAN_CTXT_CFM_HDL_BIT);
    }

    REG_SW_CLEAR_PROFILING(env->pthis, SW_PROF_IRQ_E2A_TXCFM);
}

/**
 ******************************************************************************
 */
bool ipc_host_tx_frames_pending(struct ipc_host_env_tag *env)
{
    int i, j;
    bool tx_frames_pending = false;

    for (i = 0; (i < IPC_TXQUEUE_CNT) && !tx_frames_pending; i++)
    {
        for (j = 0; j < nx_txuser_cnt[i]; j++)
        {
            uint32_t used_idx = env->txdesc_used_idx[i][j];
            uint32_t free_idx = env->txdesc_free_idx[i][j];

            // Check if this queue is empty or not
            if (used_idx != free_idx)
            {
                // The queue is not empty, update the flag and exit
                tx_frames_pending = true;
                break;
            }
        }
    }

    return (tx_frames_pending);
}

/**
 ******************************************************************************
 */
void *ipc_host_tx_flush(struct ipc_host_env_tag *env, const int queue_idx, const int user_pos)
{
    uint32_t used_idx = env->txdesc_used_idx[queue_idx][user_pos];
    void *host_id = env->tx_host_id[queue_idx][user_pos][used_idx & nx_txdesc_cnt_msk[queue_idx]];

    // call the external function to indicate that a TX packet is freed
    if (host_id != 0)
    {
        // Reset the host id in the array
        env->tx_host_id[queue_idx][user_pos][used_idx & nx_txdesc_cnt_msk[queue_idx]] = 0;

        // Increment the used index
        env->txdesc_used_idx[queue_idx][user_pos]++;
    }

    return (host_id);
}

/**
 ******************************************************************************
 */
void ipc_host_init(struct ipc_host_env_tag *env,
                  struct ipc_host_cb_tag *cb,
                  struct ipc_shared_env_tag *shared_env_ptr,
                  void *pthis)
{
    unsigned int i;
    unsigned int size;
    unsigned int * dst;

    // Reset the environments
    // Reset the IPC Shared memory
#if 0
    /* check potential platform bug on multiple stores */
    memset(shared_env_ptr, 0, sizeof(struct ipc_shared_env_tag));
#else
    dst = (unsigned int *)shared_env_ptr;
    size = (unsigned int)sizeof(struct ipc_shared_env_tag);
    for (i=0; i < size; i+=4)
    {
        *dst++ = 0;
    }
#endif
    // Reset the IPC Host environment
    memset(env, 0, sizeof(struct ipc_host_env_tag));

    // Initialize the shared environment pointer
    env->shared = shared_env_ptr;

    // Save the callbacks in our own environment
    env->cb = *cb;

    // Save the pointer to the register base
    env->pthis = pthis;

    // Initialize buffers numbers and buffers sizes needed for DMA Receptions
    env->rx_bufnb = IPC_RXBUF_CNT;
    #ifdef CONFIG_RWNX_FULLMAC
    env->rxdesc_nb = IPC_RXDESC_CNT;
    #endif //(CONFIG_RWNX_FULLMAC)
    env->radar_bufnb = IPC_RADARBUF_CNT;
    env->radar_bufsz = sizeof(struct radar_pulse_array_desc);
    env->unsuprxvec_bufnb = IPC_UNSUPRXVECBUF_CNT;
    env->unsuprxvec_bufsz = max(sizeof(struct rx_vector_desc), (size_t) RADIOTAP_HDR_MAX_LEN) +
                            RADIOTAP_HDR_VEND_MAX_LEN +  UNSUP_RX_VEC_DATA_LEN;
    env->ipc_e2amsg_bufnb = IPC_MSGE2A_BUF_CNT;
    env->ipc_e2amsg_bufsz = sizeof(struct ipc_e2a_msg);
    env->ipc_dbg_bufnb = IPC_DBGBUF_CNT;
    env->ipc_dbg_bufsz = sizeof(struct ipc_dbg_msg);

    for (i = 0; i < CONFIG_USER_MAX; i++)
    {
        // Initialize the pointers to the hostid arrays
        env->tx_host_id[0][i] = env->tx_host_id0[i];
        env->tx_host_id[1][i] = env->tx_host_id1[i];
        env->tx_host_id[2][i] = env->tx_host_id2[i];
        env->tx_host_id[3][i] = env->tx_host_id3[i];
        #if NX_TXQ_CNT == 5
        env->tx_host_id[4][i] = NULL;
        #endif

        // Initialize the pointers to the TX descriptor arrays
        env->txdesc[0][i] = shared_env_ptr->txdesc0[i];
        env->txdesc[1][i] = shared_env_ptr->txdesc1[i];
        env->txdesc[2][i] = shared_env_ptr->txdesc2[i];
        env->txdesc[3][i] = shared_env_ptr->txdesc3[i];
        #if NX_TXQ_CNT == 5
        env->txdesc[4][i] = NULL;
        #endif
    }

    #if NX_TXQ_CNT == 5
    env->tx_host_id[4][0] = env->tx_host_id4[0];
    env->txdesc[4][0] = shared_env_ptr->txdesc4[0];
    #endif
}

/**
 ******************************************************************************
 */
void ipc_host_patt_addr_push(struct ipc_host_env_tag *env, uint32_t addr)
{
    struct ipc_shared_env_tag *shared_env_ptr = env->shared;

    // Copy the address
    shared_env_ptr->pattern_addr = addr;
}

/**
 ******************************************************************************
 */
int ipc_host_rxbuf_push(struct ipc_host_env_tag *env,
#ifdef CONFIG_RWNX_FULLMAC
                        uint32_t hostid,
#endif
                        uint32_t hostbuf)
{
    struct ipc_shared_env_tag *shared_env_ptr = env->shared;

    REG_SW_CLEAR_HOSTBUF_IDX_PROFILING(env->pthis);
    REG_SW_SET_HOSTBUF_IDX_PROFILING(env->pthis, env->ipc_host_rxbuf_idx);

#ifdef CONFIG_RWNX_FULLMAC
    // Copy the hostbuf (DMA address) in the ipc shared memory
    shared_env_ptr->host_rxbuf[env->ipc_host_rxbuf_idx].hostid   = hostid;
    shared_env_ptr->host_rxbuf[env->ipc_host_rxbuf_idx].dma_addr = hostbuf;
#else
    // Save the hostid and the hostbuf in global array
    env->ipc_host_rxbuf_array[env->ipc_host_rxbuf_idx].hostid = hostid;
    env->ipc_host_rxbuf_array[env->ipc_host_rxbuf_idx].dma_addr = hostbuf;

    shared_env_ptr->host_rxbuf[env->ipc_host_rxbuf_idx] = hostbuf;
#endif //(CONFIG_RWNX_FULLMAC)

    // Signal to the embedded CPU that at least one buffer is available
    ipc_app2emb_trigger_set(shared_env_ptr, IPC_IRQ_A2E_RXBUF_BACK);

    // Increment the array index
    env->ipc_host_rxbuf_idx = (env->ipc_host_rxbuf_idx +1)%IPC_RXBUF_CNT;

    return (0);
}

#ifdef CONFIG_RWNX_FULLMAC
/**
 ******************************************************************************
 */
int ipc_host_rxdesc_push(struct ipc_host_env_tag *env, void *hostid,
                         uint32_t hostbuf)
{
    struct ipc_shared_env_tag *shared_env_ptr = env->shared;

    // Reset the RX Descriptor DMA Address and increment the counter
    env->ipc_host_rxdesc_array[env->ipc_host_rxdesc_idx].dma_addr = hostbuf;
    env->ipc_host_rxdesc_array[env->ipc_host_rxdesc_idx].hostid = hostid;

    shared_env_ptr->host_rxdesc[env->ipc_host_rxdesc_idx].dma_addr = hostbuf;

    // Signal to the embedded CPU that at least one descriptor is available
    ipc_app2emb_trigger_set(shared_env_ptr, IPC_IRQ_A2E_RXDESC_BACK);

    env->ipc_host_rxdesc_idx = (env->ipc_host_rxdesc_idx + 1) % IPC_RXDESC_CNT;

    return (0);
}
#endif /* CONFIG_RWNX_FULLMAC */

/**
 ******************************************************************************
 */
int ipc_host_radarbuf_push(struct ipc_host_env_tag *env, void *hostid,
                           uint32_t hostbuf)
{
    struct ipc_shared_env_tag *shared_env_ptr = env->shared;

    // Save the hostid and the hostbuf in global array
    env->ipc_host_radarbuf_array[env->ipc_host_radarbuf_idx].hostid = hostid;
    env->ipc_host_radarbuf_array[env->ipc_host_radarbuf_idx].dma_addr = hostbuf;

    // Copy the hostbuf (DMA address) in the ipc shared memory
    shared_env_ptr->radarbuf_hostbuf[env->ipc_host_radarbuf_idx] = hostbuf;

    // Increment the array index
    env->ipc_host_radarbuf_idx = (env->ipc_host_radarbuf_idx +1)%IPC_RADARBUF_CNT;

    return (0);
}

/**
 ******************************************************************************
 */

int ipc_host_unsup_rx_vec_buf_push(struct ipc_host_env_tag *env,
                                   void *hostid,
                                   uint32_t hostbuf)
{
    struct ipc_shared_env_tag *shared_env_ptr = env->shared;

    env->ipc_host_unsuprxvecbuf_array[env->ipc_host_unsuprxvecbuf_idx].hostid = hostid;
    env->ipc_host_unsuprxvecbuf_array[env->ipc_host_unsuprxvecbuf_idx].dma_addr = hostbuf;

    // Copy the hostbuf (DMA address) in the ipc shared memory
    shared_env_ptr->unsuprxvecbuf_hostbuf[env->ipc_host_unsuprxvecbuf_idx] = hostbuf;

    // Increment the array index
    env->ipc_host_unsuprxvecbuf_idx = (env->ipc_host_unsuprxvecbuf_idx + 1)%IPC_UNSUPRXVECBUF_CNT;

    return (0);
}

/**
 ******************************************************************************
 */
int ipc_host_msgbuf_push(struct ipc_host_env_tag *env, void *hostid,
                         uint32_t hostbuf)
{
    struct ipc_shared_env_tag *shared_env_ptr = env->shared;

    // Save the hostid and the hostbuf in global array
    env->ipc_host_msgbuf_array[env->ipc_host_msge2a_idx].hostid = hostid;
    env->ipc_host_msgbuf_array[env->ipc_host_msge2a_idx].dma_addr = hostbuf;

    // Copy the hostbuf (DMA address) in the ipc shared memory
    shared_env_ptr->msg_e2a_hostbuf_addr[env->ipc_host_msge2a_idx] = hostbuf;

    // Increment the array index
    env->ipc_host_msge2a_idx = (env->ipc_host_msge2a_idx +1)%IPC_MSGE2A_BUF_CNT;

    return (0);
}

/**
 ******************************************************************************
 */
int ipc_host_dbgbuf_push(struct ipc_host_env_tag *env, void *hostid,
                         uint32_t hostbuf)
{
    struct ipc_shared_env_tag *shared_env_ptr = env->shared;

    // Save the hostid and the hostbuf in global array
    env->ipc_host_dbgbuf_array[env->ipc_host_dbg_idx].hostid = hostid;
    env->ipc_host_dbgbuf_array[env->ipc_host_dbg_idx].dma_addr = hostbuf;

    // Copy the hostbuf (DMA address) in the ipc shared memory
    shared_env_ptr->dbg_hostbuf_addr[env->ipc_host_dbg_idx] = hostbuf;

    // Increment the array index
    env->ipc_host_dbg_idx = (env->ipc_host_dbg_idx +1)%IPC_DBGBUF_CNT;

    return (0);
}

/**
 ******************************************************************************
 */
void ipc_host_dbginfobuf_push(struct ipc_host_env_tag *env, uint32_t infobuf)
{
    struct ipc_shared_env_tag *shared_env_ptr = env->shared;

    // Copy the hostbuf (DMA address) in the ipc shared memory
    shared_env_ptr->la_dbginfo_addr = infobuf;
}

/**
 ******************************************************************************
 */
volatile struct txdesc_host *ipc_host_txdesc_get(struct ipc_host_env_tag *env, const int queue_idx, const int user_pos)
{
    volatile struct txdesc_host *txdesc_free;
    uint32_t used_idx = env->txdesc_used_idx[queue_idx][user_pos];
    uint32_t free_idx = env->txdesc_free_idx[queue_idx][user_pos];

    ASSERT_ERR(queue_idx < IPC_TXQUEUE_CNT);
    ASSERT_ERR((free_idx - used_idx) <= nx_txdesc_cnt[queue_idx]);

    // Check if a free descriptor is available
    if (free_idx != (used_idx + nx_txdesc_cnt[queue_idx]))
    {
        // Get the pointer to the first free descriptor
        txdesc_free = env->txdesc[queue_idx][user_pos] + (free_idx & nx_txdesc_cnt_msk[queue_idx]);
    }
    else
    {
        txdesc_free = NULL;
    }

    return txdesc_free;
}

/**
 ******************************************************************************
 */
void ipc_host_txdesc_push(struct ipc_host_env_tag *env, const int queue_idx,
                          const int user_pos, void *host_id)
{
    uint32_t free_idx = env->txdesc_free_idx[queue_idx][user_pos] & nx_txdesc_cnt_msk[queue_idx];
    volatile struct txdesc_host *txdesc_pushed = env->txdesc[queue_idx][user_pos] + free_idx;


    // Descriptor is now ready
    txdesc_pushed->ready = 0xFFFFFFFF;

    // Save the host id in the environment
    env->tx_host_id[queue_idx][user_pos][free_idx] = host_id;

    // Increment the index
    env->txdesc_free_idx[queue_idx][user_pos]++;

    // trigger interrupt!!!
    //REG_SW_SET_PROFILING(env->pthis, CO_BIT(queue_idx+SW_PROF_IRQ_A2E_TXDESC_FIRSTBIT));
    ipc_app2emb_trigger_setf(env->shared, CO_BIT(user_pos + queue_idx * CONFIG_USER_MAX +
                                                 IPC_IRQ_A2E_TXDESC_FIRSTBIT));
}

/**
 ******************************************************************************
 */
void ipc_host_irq(struct ipc_host_env_tag *env, uint32_t status)
{
    // Acknowledge the pending interrupts
    ipc_emb2app_ack_clear(env->shared, status);
    // And re-read the status, just to be sure that the acknowledgment is
    // effective when we start the interrupt handling
    ipc_emb2app_status_get(env->shared);

    // Optimized for only one IRQ at a time
    if (status & IPC_IRQ_E2A_RXDESC)
    {
        // handle the RX descriptor reception
        ipc_host_rxdesc_handler(env);
    }
    if (status & IPC_IRQ_E2A_MSG_ACK)
    {
        ipc_host_msgack_handler(env);
    }
    if (status & IPC_IRQ_E2A_MSG)
    {
        ipc_host_msg_handler(env);
    }
    if (status & IPC_IRQ_E2A_TXCFM)
    {
        int i;

#ifdef __KERNEL__
        spin_lock_bh(&((struct rwnx_hw *)env->pthis)->tx_lock);
#endif
        // handle the TX confirmation reception
        for (i = 0; i < IPC_TXQUEUE_CNT; i++)
        {
            int j = 0;
#ifdef CONFIG_RWNX_MUMIMO_TX
            for (; j < nx_txuser_cnt[i]; j++)
#endif
            {
                uint32_t q_bit = CO_BIT(j + i * CONFIG_USER_MAX + IPC_IRQ_E2A_TXCFM_POS);
                if (status & q_bit)
                {
                    // handle the confirmation
                    ipc_host_tx_cfm_handler(env, i, j);
                }
            }
        }
#ifdef __KERNEL__
        spin_unlock_bh(&((struct rwnx_hw *)env->pthis)->tx_lock);
#endif
    }
    if (status & IPC_IRQ_E2A_RADAR)
    {
        // handle the radar event reception
        ipc_host_radar_handler(env);
    }

    if (status & IPC_IRQ_E2A_UNSUP_RX_VEC)
    {
        // handle the unsupported rx vector reception
        ipc_host_unsup_rx_vec_handler(env);
    }

    if (status & IPC_IRQ_E2A_DBG)
    {
        ipc_host_dbg_handler(env);
    }

    if (status & IPC_IRQ_E2A_TBTT_PRIM)
    {
        env->cb.prim_tbtt_ind(env->pthis);
    }

    if (status & IPC_IRQ_E2A_TBTT_SEC)
    {
        env->cb.sec_tbtt_ind(env->pthis);
    }
}

/**
 ******************************************************************************
 */
int ipc_host_msg_push(struct ipc_host_env_tag *env, void *msg_buf, uint16_t len)
{
    int i;
    uint32_t *src, *dst;

    REG_SW_SET_PROFILING(env->pthis, SW_PROF_IPC_MSGPUSH);

    ASSERT_ERR(!env->msga2e_hostid);
    ASSERT_ERR(round_up(len, 4) <= sizeof(env->shared->msg_a2e_buf.msg));

    // Copy the message into the IPC MSG buffer
#ifdef __KERNEL__
    src = (uint32_t*)((struct rwnx_cmd *)msg_buf)->a2e_msg;
#else
    src = (uint32_t*) msg_buf;
#endif
    dst = (uint32_t*)&(env->shared->msg_a2e_buf.msg);

    // Copy the message in the IPC queue
    for (i=0; i<len; i+=4)
    {
        *dst++ = *src++;
    }

    env->msga2e_hostid = msg_buf;

    // Trigger the irq to send the message to EMB
    ipc_app2emb_trigger_set(env->shared, IPC_IRQ_A2E_MSG);

    REG_SW_CLEAR_PROFILING(env->pthis, SW_PROF_IPC_MSGPUSH);

    return (0);
}

/**
 ******************************************************************************
 */
void ipc_host_enable_irq(struct ipc_host_env_tag *env, uint32_t value)
{
    // Enable the handled interrupts
    ipc_emb2app_unmask_set(env->shared, value);
}

/**
 ******************************************************************************
 */
void ipc_host_disable_irq(struct ipc_host_env_tag *env, uint32_t value)
{
    // Enable the handled interrupts
    ipc_emb2app_unmask_clear(env->shared, value);
}

/**
 ******************************************************************************
 */
uint32_t ipc_host_get_status(struct ipc_host_env_tag *env)
{
    volatile uint32_t status;

    status = ipc_emb2app_status_get(env->shared);

    return status;
}

/**
 ******************************************************************************
 */
uint32_t ipc_host_get_rawstatus(struct ipc_host_env_tag *env)
{
    volatile uint32_t rawstatus;

    rawstatus = ipc_emb2app_rawstatus_get(env->shared);

    return rawstatus;
}

