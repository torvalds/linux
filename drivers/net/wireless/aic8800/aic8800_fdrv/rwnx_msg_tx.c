/**
 ******************************************************************************
 *
 * @file rwnx_msg_tx.c
 *
 * @brief TX function definitions
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */

#include "rwnx_msg_tx.h"
#include "rwnx_mod_params.h"
#include "reg_access.h"
#ifdef CONFIG_RWNX_BFMER
#include "rwnx_bfmer.h"
#endif //(CONFIG_RWNX_BFMER)
#include "rwnx_compat.h"
#include "rwnx_cmds.h"
#include "rwnx_main.h"
#include "aicwf_txrxif.h"
#include "rwnx_strs.h"


const struct mac_addr mac_addr_bcst = {{0xFFFF, 0xFFFF, 0xFFFF}};

/* Default MAC Rx filters that can be changed by mac80211
 * (via the configure_filter() callback) */
#define RWNX_MAC80211_CHANGEABLE        (                                       \
                                         NXMAC_ACCEPT_BA_BIT                  | \
                                         NXMAC_ACCEPT_BAR_BIT                 | \
                                         NXMAC_ACCEPT_OTHER_DATA_FRAMES_BIT   | \
                                         NXMAC_ACCEPT_PROBE_REQ_BIT           | \
                                         NXMAC_ACCEPT_PS_POLL_BIT               \
                                        )

/* Default MAC Rx filters that cannot be changed by mac80211 */
#define RWNX_MAC80211_NOT_CHANGEABLE    (                                       \
                                         NXMAC_ACCEPT_QO_S_NULL_BIT           | \
                                         NXMAC_ACCEPT_Q_DATA_BIT              | \
                                         NXMAC_ACCEPT_DATA_BIT                | \
                                         NXMAC_ACCEPT_OTHER_MGMT_FRAMES_BIT   | \
                                         NXMAC_ACCEPT_MY_UNICAST_BIT          | \
                                         NXMAC_ACCEPT_BROADCAST_BIT           | \
                                         NXMAC_ACCEPT_BEACON_BIT              | \
                                         NXMAC_ACCEPT_PROBE_RESP_BIT            \
                                        )

/* Default MAC Rx filter */
#define RWNX_DEFAULT_RX_FILTER  (RWNX_MAC80211_CHANGEABLE | RWNX_MAC80211_NOT_CHANGEABLE)

const int bw2chnl[] = {
    [NL80211_CHAN_WIDTH_20_NOHT] = PHY_CHNL_BW_20,
    [NL80211_CHAN_WIDTH_20]      = PHY_CHNL_BW_20,
    [NL80211_CHAN_WIDTH_40]      = PHY_CHNL_BW_40,
    [NL80211_CHAN_WIDTH_80]      = PHY_CHNL_BW_80,
    [NL80211_CHAN_WIDTH_160]     = PHY_CHNL_BW_160,
    [NL80211_CHAN_WIDTH_80P80]   = PHY_CHNL_BW_80P80,
};

const int chnl2bw[] = {
    [PHY_CHNL_BW_20]      = NL80211_CHAN_WIDTH_20,
    [PHY_CHNL_BW_40]      = NL80211_CHAN_WIDTH_40,
    [PHY_CHNL_BW_80]      = NL80211_CHAN_WIDTH_80,
    [PHY_CHNL_BW_160]     = NL80211_CHAN_WIDTH_160,
    [PHY_CHNL_BW_80P80]   = NL80211_CHAN_WIDTH_80P80,
};

#define RWNX_CMD_ARRAY_SIZE 20
#define RWNX_CMD_HIGH_WATER_SIZE RWNX_CMD_ARRAY_SIZE/2
//#define RWNX_MSG_ARRAY_SIZE 20

struct rwnx_cmd cmd_array[RWNX_CMD_ARRAY_SIZE];
//struct lmac_msg msg_array[RWNX_MSG_ARRAY_SIZE];

static spinlock_t cmd_array_lock;
//static spinlock_t msg_array_lock;

//int msg_array_index = 0;
int cmd_array_index = 0;



/*****************************************************************************/
/*
 * Parse the ampdu density to retrieve the value in usec, according to the
 * values defined in ieee80211.h
 */
static inline u8 rwnx_ampdudensity2usec(u8 ampdudensity)
{
    switch (ampdudensity) {
    case IEEE80211_HT_MPDU_DENSITY_NONE:
        return 0;
        /* 1 microsecond is our granularity */
    case IEEE80211_HT_MPDU_DENSITY_0_25:
    case IEEE80211_HT_MPDU_DENSITY_0_5:
    case IEEE80211_HT_MPDU_DENSITY_1:
        return 1;
    case IEEE80211_HT_MPDU_DENSITY_2:
        return 2;
    case IEEE80211_HT_MPDU_DENSITY_4:
        return 4;
    case IEEE80211_HT_MPDU_DENSITY_8:
        return 8;
    case IEEE80211_HT_MPDU_DENSITY_16:
        return 16;
    default:
        return 0;
    }
}

static inline bool use_pairwise_key(struct cfg80211_crypto_settings *crypto)
{
    if ((crypto->cipher_group ==  WLAN_CIPHER_SUITE_WEP40) ||
        (crypto->cipher_group ==  WLAN_CIPHER_SUITE_WEP104))
        return false;

    return true;
}

static inline bool is_non_blocking_msg(int id)
{
    return ((id == MM_TIM_UPDATE_REQ) || (id == ME_RC_SET_RATE_REQ) ||
            (id == MM_BFMER_ENABLE_REQ) || (id == ME_TRAFFIC_IND_REQ) ||
            (id == TDLS_PEER_TRAFFIC_IND_REQ) ||
            (id == MESH_PATH_CREATE_REQ) || (id == MESH_PROXY_ADD_REQ) ||
            (id == SM_EXTERNAL_AUTH_REQUIRED_RSP));
}

static inline u8_l get_chan_flags(uint32_t flags)
{
    u8_l chan_flags = 0;
#ifdef RADAR_OR_IR_DETECT
    #if LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)
    if (flags & IEEE80211_CHAN_PASSIVE_SCAN)
    #else
    if (flags & IEEE80211_CHAN_NO_IR)
    #endif
        chan_flags |= CHAN_NO_IR;
    if (flags & IEEE80211_CHAN_RADAR)
        chan_flags |= CHAN_RADAR;
#endif
    return chan_flags;
}

static inline s8_l chan_to_fw_pwr(int power)
{
    return power>127?127:(s8_l)power;
}

static inline void limit_chan_bw(u8_l *bw, u16_l primary, u16_l *center1)
{
    int oft, new_oft = 10;

    if (*bw <= PHY_CHNL_BW_40)
        return;

    oft = *center1 - primary;
    *bw = PHY_CHNL_BW_40;

    if (oft < 0)
        new_oft = new_oft * -1;
    if (abs(oft) == 10 || abs(oft) == 50)
        new_oft = new_oft * -1;

    *center1 = primary + new_oft;
}

struct rwnx_cmd *rwnx_cmd_malloc(void){
	struct rwnx_cmd *cmd = NULL;
	unsigned long flags = 0;

	spin_lock_irqsave(&cmd_array_lock, flags);

	for(cmd_array_index = 0; cmd_array_index < RWNX_CMD_ARRAY_SIZE; cmd_array_index++){
		if(cmd_array[cmd_array_index].used == 0){
			AICWFDBG(LOGTRACE, "%s get cmd_array[%d]:%p \r\n", __func__, cmd_array_index,&cmd_array[cmd_array_index]);
			cmd = &cmd_array[cmd_array_index];
			cmd_array[cmd_array_index].used = 1;
			break;
		}
	}

	if(cmd_array_index >= RWNX_CMD_HIGH_WATER_SIZE){
		AICWFDBG(LOGERROR, "%s cmd(%d) was pending...\r\n", __func__, cmd_array_index);
		mdelay(100);
	}

	if(!cmd){
		AICWFDBG(LOGERROR, "%s array is empty...\r\n", __func__);
	}

	spin_unlock_irqrestore(&cmd_array_lock, flags);

	return cmd;
}

void rwnx_cmd_free(struct rwnx_cmd *cmd){
	unsigned long flags = 0;

	spin_lock_irqsave(&cmd_array_lock, flags);
	cmd->used = 0;
	AICWFDBG(LOGTRACE, "%s cmd_array[%d]:%p \r\n", __func__, cmd->array_id, cmd);
	spin_unlock_irqrestore(&cmd_array_lock, flags);
}


int rwnx_init_cmd_array(void){

	AICWFDBG(LOGTRACE, "%s Enter \r\n", __func__);
	spin_lock_init(&cmd_array_lock);

	for(cmd_array_index = 0; cmd_array_index < RWNX_CMD_ARRAY_SIZE; cmd_array_index++){
		AICWFDBG(LOGTRACE, "%s cmd_queue[%d]:%p \r\n", __func__, cmd_array_index, &cmd_array[cmd_array_index]);
		cmd_array[cmd_array_index].used = 0;
		cmd_array[cmd_array_index].array_id = cmd_array_index;
	}
	AICWFDBG(LOGTRACE, "%s Exit \r\n", __func__);

	return 0;
}

void rwnx_free_cmd_array(void){

	AICWFDBG(LOGTRACE, "%s Enter \r\n", __func__);

	for(cmd_array_index = 0; cmd_array_index < RWNX_CMD_ARRAY_SIZE; cmd_array_index++){
		cmd_array[cmd_array_index].used = 0;
	}

	AICWFDBG(LOGTRACE, "%s Exit \r\n", __func__);
}


#if 0
int rwnx_init_msg_array(void){

	printk("%s enter\r\n", __func__);
	spin_lock_init(&msg_array_lock);

	for(msg_array_index = 0; msg_array_index < RWNX_MSG_ARRAY_SIZE; msg_array_index++){
		printk("%s msg_queue[%d]:%p \r\n", __func__, msg_array_index, &msg_array[msg_array_index]);
	}
	printk("%s exit\r\n", __func__);

	return 0;

}


void rwnx_free_msg_array(void){

	printk("%s enter\r\n", __func__);
#if 1
	for(msg_array_index = 0; msg_array_index < RWNX_MSG_ARRAY_SIZE; msg_array_index++){
		msg_array[msg_array_index].param_len = 0;
	}
#endif
	printk("%s exit\r\n", __func__);
}

struct lmac_msg *rwnx_msg_malloc_(void){
	struct lmac_msg *msg = NULL;


	spin_lock(&msg_array_lock);
	printk("%s enter\r\n", __func__);
	for(msg_array_index = 0; msg_array_index < RWNX_MSG_ARRAY_SIZE; msg_array_index++){
		if(msg_array[msg_array_index].param_len== 0){
			printk("%s get msg_array[%d]:%p \r\n", __func__, msg_array_index, &msg_array[msg_array_index]);
			msg = &msg_array[msg_array_index];
			break;
		}else{
			printk("%s msg_array[%d] in used param_len= %d \r\n",
				__func__,
				msg_array_index,
				msg_array[msg_array_index].param_len);
		}
	}

	if(!msg){
		printk("%s array is empty...\r\n", __func__);
	}
	spin_unlock(&msg_array_lock);

	return msg;
}

void rwnx_msg_free_(struct lmac_msg *msg){

	spin_lock(&msg_array_lock);
	printk("%s enter \r\n", __func__);

	for(msg_array_index = 0; msg_array_index < RWNX_MSG_ARRAY_SIZE; msg_array_index++){
		if(msg == &msg_array[msg_array_index]){
			break;
		}
	}

	memset(msg->param, 0, msg->param_len);
	msg->id = 0;
	msg->dest_id = 0;
	msg->src_id = 0;
	msg->param_len = 0;

	printk("%s msg_array[%d]:%p \r\n", __func__, msg_array_index, msg);
	spin_unlock(&msg_array_lock);
}
#endif


/**
 ******************************************************************************
 * @brief Allocate memory for a message
 *
 * This primitive allocates memory for a message that has to be sent. The memory
 * is allocated dynamically on the heap and the length of the variable parameter
 * structure has to be provided in order to allocate the correct size.
 *
 * Several additional parameters are provided which will be preset in the message
 * and which may be used internally to choose the kind of memory to allocate.
 *
 * The memory allocated will be automatically freed by the kernel, after the
 * pointer has been sent to ke_msg_send(). If the message is not sent, it must
 * be freed explicitly with ke_msg_free().
 *
 * Allocation failure is considered critical and should not happen.
 *
 * @param[in] id        Message identifier
 * @param[in] dest_id   Destination Task Identifier
 * @param[in] src_id    Source Task Identifier
 * @param[in] param_len Size of the message parameters to be allocated
 *
 * @return Pointer to the parameter member of the ke_msg. If the parameter
 *         structure is empty, the pointer will point to the end of the message
 *         and should not be used (except to retrieve the message pointer or to
 *         send the message)
 ******************************************************************************
 */
static inline void *rwnx_msg_zalloc(lmac_msg_id_t const id,
                                    lmac_task_id_t const dest_id,
                                    lmac_task_id_t const src_id,
                                    uint16_t const param_len)
{
    struct lmac_msg *msg;
    gfp_t flags;

    //if (is_non_blocking_msg(id) && in_softirq())
    flags = GFP_ATOMIC;
    //else
    //    flags = GFP_KERNEL;

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
	//printk("rwnx_msg_zalloc size=%d  id=%d\n",msg->param_len,msg->id);

    return msg->param;
}

static void rwnx_msg_free(struct rwnx_hw *rwnx_hw, const void *msg_params)
{
    struct lmac_msg *msg = container_of((void *)msg_params,
                                        struct lmac_msg, param);

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Free the message */
    kfree(msg);
}



static int rwnx_send_msg(struct rwnx_hw *rwnx_hw, const void *msg_params,
                         int reqcfm, lmac_msg_id_t reqid, void *cfm)
{
    struct lmac_msg *msg;
    struct rwnx_cmd *cmd;
    bool nonblock;
    int ret = 0;
    u8_l empty = 0;

    //RWNX_DBG(RWNX_FN_ENTRY_STR);
    AICWFDBG(LOGDEBUG, "%s (%d)%s reqcfm:%d in_softirq:%d in_atomic:%d\r\n",
    __func__, reqid, RWNX_ID2STR(reqid), reqcfm, (int)in_softirq(), (int)in_atomic());

#ifdef AICWF_USB_SUPPORT
    if (rwnx_hw->usbdev->state == USB_DOWN_ST) {
        rwnx_msg_free(rwnx_hw, msg_params);
		AICWFDBG(LOGERROR, "%s bus is down\n", __func__);
        return 0;
    }
#endif
#ifdef AICWF_SDIO_SUPPORT
    if(rwnx_hw->sdiodev->bus_if->state == BUS_DOWN_ST) {
        rwnx_msg_free(rwnx_hw, msg_params);
        sdio_err("bus is down\n");
        return 0;
    }
#endif

    msg = container_of((void *)msg_params, struct lmac_msg, param);

    #if 0
    if (!test_bit(RWNX_DEV_STARTED, &rwnx_hw->drv_flags) &&
        reqid != DBG_MEM_READ_CFM && reqid != DBG_MEM_WRITE_CFM &&
        reqid != DBG_MEM_BLOCK_WRITE_CFM && reqid != DBG_START_APP_CFM &&
        reqid != MM_SET_RF_CALIB_CFM && reqid != MM_SET_RF_CONFIG_CFM &&
        reqid != MM_RESET_CFM && reqid != MM_VERSION_CFM &&
        reqid != MM_START_CFM && reqid != MM_SET_IDLE_CFM &&
        reqid != ME_CONFIG_CFM && reqid != MM_SET_PS_MODE_CFM &&
        reqid != ME_CHAN_CONFIG_CFM) {
        printk(KERN_CRIT "%s: bypassing (RWNX_DEV_RESTARTING set) 0x%02x\n",
               __func__, reqid);
        kfree(msg);
        return -EBUSY;
    }
    #endif
#if 0
    else if (!rwnx_hw->ipc_env) {
        printk(KERN_CRIT "%s: bypassing (restart must have failed)\n", __func__);
        kfree(msg);
        return -EBUSY;
    }
#endif

    //nonblock = is_non_blocking_msg(msg->id);
    nonblock = 0;//AIDEN
    cmd = rwnx_cmd_malloc();//kzalloc(sizeof(struct rwnx_cmd), nonblock ? GFP_ATOMIC : GFP_KERNEL);
    cmd->result  = -EINTR;
    cmd->id      = msg->id;
    cmd->reqid   = reqid;
    cmd->a2e_msg = msg;
    cmd->e2a_msg = cfm;
    if (nonblock)
        cmd->flags = RWNX_CMD_FLAG_NONBLOCK;
    if (reqcfm)
        cmd->flags |= RWNX_CMD_FLAG_REQ_CFM;

    if(cfm != NULL) {
        do {
            if(rwnx_hw->cmd_mgr->state == RWNX_CMD_MGR_STATE_CRASHED)
		break;
            spin_lock_bh(&rwnx_hw->cmd_mgr->lock);
            empty = list_empty(&rwnx_hw->cmd_mgr->cmds);
            spin_unlock_bh(&rwnx_hw->cmd_mgr->lock);
            if(!empty) {
		if(in_softirq()) {
			#ifdef CONFIG_RWNX_DBG
				AICWFDBG(LOGDEBUG, "in_softirq:check cmdqueue empty\n");
			#endif
			mdelay(10);
		}
		else {
			#ifdef CONFIG_RWNX_DBG
				AICWFDBG(LOGDEBUG, "check cmdqueue empty\n");
			#endif
			msleep(50);
		}
             }
	} while(!empty);//wait for cmd queue empty
    }

    if(reqcfm) {
        cmd->flags &= ~RWNX_CMD_FLAG_WAIT_ACK; // we don't need ack any more
        ret = rwnx_hw->cmd_mgr->queue(rwnx_hw->cmd_mgr, cmd);
    } else {
#ifdef AICWF_SDIO_SUPPORT
        aicwf_set_cmd_tx((void *)(rwnx_hw->sdiodev), cmd->a2e_msg, sizeof(struct lmac_msg) + cmd->a2e_msg->param_len);
#else
        aicwf_set_cmd_tx((void *)(rwnx_hw->usbdev), cmd->a2e_msg, sizeof(struct lmac_msg) + cmd->a2e_msg->param_len);
#endif
    }

    if(!reqcfm || ret)
        rwnx_cmd_free(cmd);//kfree(cmd);

    return ret;//0;
}


static int rwnx_send_msg1(struct rwnx_hw *rwnx_hw, const void *msg_params,
                         int reqcfm, lmac_msg_id_t reqid, void *cfm, bool defer)
{
    struct lmac_msg *msg;
    struct rwnx_cmd *cmd;
    bool nonblock;
    int ret = 0;

    //RWNX_DBG(RWNX_FN_ENTRY_STR);

	AICWFDBG(LOGDEBUG,"%s (%d)%s reqcfm:%d in_softirq:%d in_atomic:%d\r\n",
    	__func__, reqid, RWNX_ID2STR(reqid), reqcfm, (int)in_softirq(), (int)in_atomic());

    if (rwnx_hw->usbdev->state == USB_DOWN_ST) {
        rwnx_msg_free(rwnx_hw, msg_params);
		AICWFDBG(LOGERROR, "%s bus is down\n", __func__);
        return 0;
    }


    msg = container_of((void *)msg_params, struct lmac_msg, param);

    //nonblock = is_non_blocking_msg(msg->id);
	nonblock = 0;
    cmd = rwnx_cmd_malloc();//kzalloc(sizeof(struct rwnx_cmd), nonblock ? GFP_ATOMIC : GFP_KERNEL);
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
        if(!defer)
            ret = rwnx_hw->cmd_mgr->queue(rwnx_hw->cmd_mgr, cmd);
        else
            ret = cmd_mgr_queue_force_defer(rwnx_hw->cmd_mgr, cmd);
    }

    if (!reqcfm || ret) {
        rwnx_cmd_free(cmd);//kfree(cmd);
    }

    if (!ret) {
        ret = cmd->result;
    }

    //return ret;
    return 0;
}

/******************************************************************************
 *    Control messages handling functions (FULLMAC)
 *****************************************************************************/
int rwnx_send_reset(struct rwnx_hw *rwnx_hw)
{
    void *void_param;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* RESET REQ has no parameter */
    void_param = rwnx_msg_zalloc(MM_RESET_REQ, TASK_MM, DRV_TASK_ID, 0);
    if (!void_param)
        return -ENOMEM;

    return rwnx_send_msg(rwnx_hw, void_param, 1, MM_RESET_CFM, NULL);
}

int rwnx_send_start(struct rwnx_hw *rwnx_hw)
{
    struct mm_start_req *start_req_param;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the START REQ message */
    start_req_param = rwnx_msg_zalloc(MM_START_REQ, TASK_MM, DRV_TASK_ID,
                                      sizeof(struct mm_start_req));
    if (!start_req_param)
        return -ENOMEM;

    /* Set parameters for the START message */
    memcpy(&start_req_param->phy_cfg, &rwnx_hw->phy.cfg, sizeof(rwnx_hw->phy.cfg));
    start_req_param->uapsd_timeout = (u32_l)rwnx_hw->mod_params->uapsd_timeout;
    start_req_param->lp_clk_accuracy = (u16_l)rwnx_hw->mod_params->lp_clk_ppm;

    /* Send the START REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, start_req_param, 1, MM_START_CFM, NULL);
}

int rwnx_send_version_req(struct rwnx_hw *rwnx_hw, struct mm_version_cfm *cfm)
{
    void *void_param;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* VERSION REQ has no parameter */
    void_param = rwnx_msg_zalloc(MM_VERSION_REQ, TASK_MM, DRV_TASK_ID, 0);
    if (!void_param)
        return -ENOMEM;

    return rwnx_send_msg(rwnx_hw, void_param, 1, MM_VERSION_CFM, cfm);
}

int rwnx_send_add_if(struct rwnx_hw *rwnx_hw, const unsigned char *mac,
                     enum nl80211_iftype iftype, bool p2p, struct mm_add_if_cfm *cfm)
{
    struct mm_add_if_req *add_if_req_param;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the ADD_IF_REQ message */
    add_if_req_param = rwnx_msg_zalloc(MM_ADD_IF_REQ, TASK_MM, DRV_TASK_ID,
                                       sizeof(struct mm_add_if_req));
    if (!add_if_req_param)
        return -ENOMEM;

    /* Set parameters for the ADD_IF_REQ message */
    memcpy(&(add_if_req_param->addr.array[0]), mac, ETH_ALEN);
    switch (iftype) {
    #ifdef CONFIG_RWNX_FULLMAC
    //case NL80211_IFTYPE_P2P_DEVICE:
    case NL80211_IFTYPE_P2P_CLIENT:
        add_if_req_param->p2p = true;
        // no break
    #endif /* CONFIG_RWNX_FULLMAC */
    case NL80211_IFTYPE_STATION:
        add_if_req_param->type = MM_STA;
        break;

    case NL80211_IFTYPE_ADHOC:
        add_if_req_param->type = MM_IBSS;
        break;

    #ifdef CONFIG_RWNX_FULLMAC
    case NL80211_IFTYPE_P2P_GO:
        add_if_req_param->p2p = true;
        // no break
    #endif /* CONFIG_RWNX_FULLMAC */
    case NL80211_IFTYPE_AP:
        add_if_req_param->type = MM_AP;
        break;
    case NL80211_IFTYPE_MESH_POINT:
        add_if_req_param->type = MM_MESH_POINT;
        break;
    case NL80211_IFTYPE_AP_VLAN:
        return -1;
    case NL80211_IFTYPE_MONITOR:
        add_if_req_param->type = MM_MONITOR;
        break;
    default:
        add_if_req_param->type = MM_STA;
        break;
    }


    /* Send the ADD_IF_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, add_if_req_param, 1, MM_ADD_IF_CFM, cfm);
}

int rwnx_send_remove_if(struct rwnx_hw *rwnx_hw, u8 vif_index, bool defer)
{
    struct mm_remove_if_req *remove_if_req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_REMOVE_IF_REQ message */
    remove_if_req = rwnx_msg_zalloc(MM_REMOVE_IF_REQ, TASK_MM, DRV_TASK_ID,
                                    sizeof(struct mm_remove_if_req));
    if (!remove_if_req)
        return -ENOMEM;

    /* Set parameters for the MM_REMOVE_IF_REQ message */
    remove_if_req->inst_nbr = vif_index;

    /* Send the MM_REMOVE_IF_REQ message to LMAC FW */
    return rwnx_send_msg1(rwnx_hw, remove_if_req, 1, MM_REMOVE_IF_CFM, NULL, defer);
}

int rwnx_send_set_channel(struct rwnx_hw *rwnx_hw, int phy_idx,
                          struct mm_set_channel_cfm *cfm)
{
    struct mm_set_channel_req *req;
    enum nl80211_chan_width width;
    u16 center_freq, center_freq1, center_freq2;
    s8 tx_power = 0;
    u8 flags;
    enum nl80211_band band;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    if (phy_idx >= rwnx_hw->phy.cnt)
        return -ENOTSUPP;

    req = rwnx_msg_zalloc(MM_SET_CHANNEL_REQ, TASK_MM, DRV_TASK_ID,
                          sizeof(struct mm_set_channel_req));
    if (!req)
        return -ENOMEM;

    if (phy_idx == 0) {
#ifdef CONFIG_RWNX_FULLMAC
        /* On FULLMAC only setting channel of secondary chain */
        wiphy_err(rwnx_hw->wiphy, "Trying to set channel of primary chain");
        return 0;
#endif /* CONFIG_RWNX_FULLMAC */
    } else {
        struct rwnx_sec_phy_chan *chan = &rwnx_hw->phy.sec_chan;

        width = chnl2bw[chan->type];
        band  = chan->band;
        center_freq  = chan->prim20_freq;
        center_freq1 = chan->center_freq1;
        center_freq2 = chan->center_freq2;
        flags = 0;
    }

    req->chan.band = band;
    req->chan.type = bw2chnl[width];
    req->chan.prim20_freq  = center_freq;
    req->chan.center1_freq = center_freq1;
    req->chan.center2_freq = center_freq2;
    req->chan.tx_power = tx_power;
    req->chan.flags = flags;
    req->index = phy_idx;

    if (rwnx_hw->phy.limit_bw)
        limit_chan_bw(&req->chan.type, req->chan.prim20_freq, &req->chan.center1_freq);

    RWNX_DBG("mac80211:   freq=%d(c1:%d - c2:%d)/width=%d - band=%d\n"
             "   hw(%d): prim20=%d(c1:%d - c2:%d)/ type=%d - band=%d\n",
             center_freq, center_freq1, center_freq2, width, band,
             phy_idx, req->chan.prim20_freq, req->chan.center1_freq,
             req->chan.center2_freq, req->chan.type, req->chan.band);

    /* Send the MM_SET_CHANNEL_REQ REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, MM_SET_CHANNEL_CFM, cfm);
}

int rwnx_send_key_add(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 sta_idx, bool pairwise,
                      u8 *key, u8 key_len, u8 key_idx, u8 cipher_suite,
                      struct mm_key_add_cfm *cfm)
{
    struct mm_key_add_req *key_add_req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_KEY_ADD_REQ message */
    key_add_req = rwnx_msg_zalloc(MM_KEY_ADD_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_key_add_req));
    if (!key_add_req)
        return -ENOMEM;

    /* Set parameters for the MM_KEY_ADD_REQ message */
    if (sta_idx != 0xFF) {
        /* Pairwise key */
        key_add_req->sta_idx = sta_idx;
    } else {
        /* Default key */
        key_add_req->sta_idx = sta_idx;
        key_add_req->key_idx = (u8_l)key_idx; /* only useful for default keys */
    }
    key_add_req->pairwise = pairwise;
    key_add_req->inst_nbr = vif_idx;
    key_add_req->key.length = key_len;
    memcpy(&(key_add_req->key.array[0]), key, key_len);

    key_add_req->cipher_suite = cipher_suite;

    RWNX_DBG("%s: sta_idx:%d key_idx:%d inst_nbr:%d cipher:%d key_len:%d\n", __func__,
             key_add_req->sta_idx, key_add_req->key_idx, key_add_req->inst_nbr,
             key_add_req->cipher_suite, key_add_req->key.length);
#if defined(CONFIG_RWNX_DBG) || defined(CONFIG_DYNAMIC_DEBUG)
    print_hex_dump_bytes("key: ", DUMP_PREFIX_OFFSET, key_add_req->key.array, key_add_req->key.length);
#endif

    /* Send the MM_KEY_ADD_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, key_add_req, 1, MM_KEY_ADD_CFM, cfm);
}

int rwnx_send_key_del(struct rwnx_hw *rwnx_hw, uint8_t hw_key_idx)
{
    struct mm_key_del_req *key_del_req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_KEY_DEL_REQ message */
    key_del_req = rwnx_msg_zalloc(MM_KEY_DEL_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_key_del_req));
    if (!key_del_req)
        return -ENOMEM;

    /* Set parameters for the MM_KEY_DEL_REQ message */
    key_del_req->hw_key_idx = hw_key_idx;

    /* Send the MM_KEY_DEL_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, key_del_req, 1, MM_KEY_DEL_CFM, NULL);
}

int rwnx_send_bcn(struct rwnx_hw *rwnx_hw,u8 *buf, u8 vif_idx, u16 bcn_len)
{
	struct apm_set_bcn_ie_req *bcn_ie_req;
	bcn_ie_req = rwnx_msg_zalloc(APM_SET_BEACON_IE_REQ, TASK_APM, DRV_TASK_ID,
							   sizeof(struct apm_set_bcn_ie_req));
	if (!bcn_ie_req)
		return -ENOMEM;

	bcn_ie_req->vif_idx = vif_idx;
	bcn_ie_req->bcn_ie_len = bcn_len;
	memcpy(bcn_ie_req->bcn_ie, (u8 *)buf, bcn_len);
    kfree(buf);

	return rwnx_send_msg(rwnx_hw, bcn_ie_req, 1, APM_SET_BEACON_IE_CFM, NULL);
}

int rwnx_send_bcn_change(struct rwnx_hw *rwnx_hw, u8 vif_idx, u32 bcn_addr,
                         u16 bcn_len, u16 tim_oft, u16 tim_len, u16 *csa_oft)
{
    struct mm_bcn_change_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_BCN_CHANGE_REQ message */
    req = rwnx_msg_zalloc(MM_BCN_CHANGE_REQ, TASK_MM, DRV_TASK_ID,
                          sizeof(struct mm_bcn_change_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the MM_BCN_CHANGE_REQ message */
    req->bcn_ptr = bcn_addr;
    req->bcn_len = bcn_len;
    req->tim_oft = tim_oft;
    req->tim_len = tim_len;
    req->inst_nbr = vif_idx;

    if (csa_oft) {
        int i;
        for (i = 0; i < BCN_MAX_CSA_CPT; i++) {
            req->csa_oft[i] = csa_oft[i];
        }
    }

    /* Send the MM_BCN_CHANGE_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, MM_BCN_CHANGE_CFM, NULL);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
static inline void cfg80211_chandef_create(struct cfg80211_chan_def *chandef,
                             struct ieee80211_channel *chan,
                             enum nl80211_channel_type chan_type)
{
        if (WARN_ON(!chan))
                return;
        chandef->chan = chan;
        chandef->center_freq2 = 0;
        switch (chan_type) {
        case NL80211_CHAN_NO_HT:
                chandef->width = NL80211_CHAN_WIDTH_20_NOHT;
                chandef->center_freq1 = chan->center_freq;
                break;
        case NL80211_CHAN_HT20:
                chandef->width = NL80211_CHAN_WIDTH_20;
                chandef->center_freq1 = chan->center_freq;
                break;
        case NL80211_CHAN_HT40PLUS:
                chandef->width = NL80211_CHAN_WIDTH_40;
                chandef->center_freq1 = chan->center_freq + 10;
                break;
        case NL80211_CHAN_HT40MINUS:
                chandef->width = NL80211_CHAN_WIDTH_40;
                chandef->center_freq1 = chan->center_freq - 10;
                break;
        default:
                WARN_ON(1);
        }
}
#endif

int rwnx_send_roc(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
                  struct ieee80211_channel *chan, unsigned  int duration,
                  struct mm_remain_on_channel_cfm *roc_cfm)
{
    struct mm_remain_on_channel_req *req;
    struct cfg80211_chan_def chandef;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Create channel definition structure */
    cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_NO_HT);

    /* Build the MM_REMAIN_ON_CHANNEL_REQ message */
    req = rwnx_msg_zalloc(MM_REMAIN_ON_CHANNEL_REQ, TASK_MM, DRV_TASK_ID,
                          sizeof(struct mm_remain_on_channel_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the MM_REMAIN_ON_CHANNEL_REQ message */
    req->op_code      = MM_ROC_OP_START;
    req->vif_index    = vif->vif_index;
    req->duration_ms  = duration;
    req->band         = chan->band;
    req->type         = bw2chnl[chandef.width];
    req->prim20_freq  = chan->center_freq;
    req->center1_freq = chandef.center_freq1;
    req->center2_freq = chandef.center_freq2;
    req->tx_power     = chan_to_fw_pwr(chan->max_power);

    /* Send the MM_REMAIN_ON_CHANNEL_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, MM_REMAIN_ON_CHANNEL_CFM, roc_cfm);
}

int rwnx_send_cancel_roc(struct rwnx_hw *rwnx_hw)
{
    struct mm_remain_on_channel_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_REMAIN_ON_CHANNEL_REQ message */
    req = rwnx_msg_zalloc(MM_REMAIN_ON_CHANNEL_REQ, TASK_MM, DRV_TASK_ID,
                          sizeof(struct mm_remain_on_channel_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the MM_REMAIN_ON_CHANNEL_REQ message */
    req->op_code = MM_ROC_OP_CANCEL;

    /* Send the MM_REMAIN_ON_CHANNEL_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, MM_REMAIN_ON_CHANNEL_CFM, NULL);
}

int rwnx_send_set_power(struct rwnx_hw *rwnx_hw, u8 vif_idx, s8 pwr,
                        struct mm_set_power_cfm *cfm)
{
    struct mm_set_power_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_SET_POWER_REQ message */
    req = rwnx_msg_zalloc(MM_SET_POWER_REQ, TASK_MM, DRV_TASK_ID,
                          sizeof(struct mm_set_power_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the MM_SET_POWER_REQ message */
    req->inst_nbr = vif_idx;
    req->power = pwr;

    /* Send the MM_SET_POWER_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, MM_SET_POWER_CFM, cfm);
}

int rwnx_send_set_edca(struct rwnx_hw *rwnx_hw, u8 hw_queue, u32 param,
                       bool uapsd, u8 inst_nbr)
{
    struct mm_set_edca_req *set_edca_req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_SET_EDCA_REQ message */
    set_edca_req = rwnx_msg_zalloc(MM_SET_EDCA_REQ, TASK_MM, DRV_TASK_ID,
                                   sizeof(struct mm_set_edca_req));
    if (!set_edca_req)
        return -ENOMEM;

    /* Set parameters for the MM_SET_EDCA_REQ message */
    set_edca_req->ac_param = param;
    set_edca_req->uapsd = uapsd;
    set_edca_req->hw_queue = hw_queue;
    set_edca_req->inst_nbr = inst_nbr;

    /* Send the MM_SET_EDCA_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, set_edca_req, 1, MM_SET_EDCA_CFM, NULL);
}

#ifdef CONFIG_RWNX_P2P_DEBUGFS
int rwnx_send_p2p_oppps_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
                            u8 ctw, struct mm_set_p2p_oppps_cfm *cfm)
{
    struct mm_set_p2p_oppps_req *p2p_oppps_req;
    int error;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_SET_P2P_OPPPS_REQ message */
    p2p_oppps_req = rwnx_msg_zalloc(MM_SET_P2P_OPPPS_REQ, TASK_MM, DRV_TASK_ID,
                                    sizeof(struct mm_set_p2p_oppps_req));

    if (!p2p_oppps_req) {
        return -ENOMEM;
    }

    /* Fill the message parameters */
    p2p_oppps_req->vif_index = rwnx_vif->vif_index;
    p2p_oppps_req->ctwindow = ctw;

    /* Send the MM_P2P_OPPPS_REQ message to LMAC FW */
    error = rwnx_send_msg(rwnx_hw, p2p_oppps_req, 1, MM_SET_P2P_OPPPS_CFM, cfm);

    return (error);
}

int rwnx_send_p2p_noa_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
                          int count, int interval, int duration, bool dyn_noa,
                          struct mm_set_p2p_noa_cfm *cfm)
{
    struct mm_set_p2p_noa_req *p2p_noa_req;
    int error;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Param check */
    if (count > 255)
        count = 255;

    if (duration >= interval) {
        dev_err(rwnx_hw->dev, "Invalid p2p NOA config: interval=%d <= duration=%d\n",
                interval, duration);
        return -EINVAL;
    }

    /* Build the MM_SET_P2P_NOA_REQ message */
    p2p_noa_req = rwnx_msg_zalloc(MM_SET_P2P_NOA_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_set_p2p_noa_req));

    if (!p2p_noa_req) {
        return -ENOMEM;
    }

    /* Fill the message parameters */
    p2p_noa_req->vif_index = rwnx_vif->vif_index;
    p2p_noa_req->noa_inst_nb = 0;
    p2p_noa_req->count = count;

    if (count) {
        p2p_noa_req->duration_us = duration * 1024;
        p2p_noa_req->interval_us = interval * 1024;
        p2p_noa_req->start_offset = (interval - duration - 10) * 1024;
        p2p_noa_req->dyn_noa = dyn_noa;
    }

    /* Send the MM_SET_2P_NOA_REQ message to LMAC FW */
    error = rwnx_send_msg(rwnx_hw, p2p_noa_req, 1, MM_SET_P2P_NOA_CFM, cfm);

    return (error);
}
#endif /* CONFIG_RWNX_P2P_DEBUGFS */

#ifdef AICWF_ARP_OFFLOAD
int rwnx_send_arpoffload_en_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
                          u32_l ipaddr,  u8_l enable)
{
    struct mm_set_arpoffload_en_req *arp_offload_req;
    int error;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_SET_ARPOFFLOAD_REQ message */
    arp_offload_req = rwnx_msg_zalloc(MM_SET_ARPOFFLOAD_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_set_arpoffload_en_req));

    if (!arp_offload_req) {
        return -ENOMEM;
    }

    /* Fill the message parameters */
	arp_offload_req->enable = enable;
	arp_offload_req->vif_idx = rwnx_vif->vif_index;
	arp_offload_req->ipaddr = ipaddr;

    /* Send the MM_ARPOFFLOAD_EN_REQ message to UMAC FW */
    error = rwnx_send_msg(rwnx_hw, arp_offload_req, 1, MM_SET_ARPOFFLOAD_CFM, NULL);

    return (error);
}
#endif

int rwnx_send_coex_req(struct rwnx_hw *rwnx_hw, u8_l disable_coexnull, u8_l enable_nullcts)
{
    struct mm_set_coex_req *coex_req;
    int error;

    RWNX_DBG(RWNX_FN_ENTRY_STR);


    /* Build the MM_SET_COEX_REQ message */
    coex_req = rwnx_msg_zalloc(MM_SET_COEX_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_set_coex_req));

    if (!coex_req) {
        return -ENOMEM;
    }

    coex_req->bt_on = 1;
    coex_req->disable_coexnull = disable_coexnull;
    coex_req->enable_nullcts = enable_nullcts;
    coex_req->enable_periodic_timer = 0;
    coex_req->coex_timeslot_set = 0;

    /* Send the MM_SET_COEX_REQ message to UMAC FW */
    error = rwnx_send_msg(rwnx_hw, coex_req, 1, MM_SET_COEX_CFM, NULL);

    return (error);
};


int rwnx_send_rf_config_req(struct rwnx_hw *rwnx_hw, u8_l ofst, u8_l sel, u8_l *tbl, u16_l len)
{
    struct mm_set_rf_config_req *rf_config_req;
    int error;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_SET_RF_CONFIG_REQ message */
    rf_config_req = rwnx_msg_zalloc(MM_SET_RF_CONFIG_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_set_rf_config_req));

    if (!rf_config_req) {
        return -ENOMEM;
    }

    rf_config_req->table_sel = sel;
    rf_config_req->table_ofst = ofst;
    rf_config_req->table_num = 16;
    rf_config_req->deft_page = 0;

	memcpy(rf_config_req->data, tbl, len);

    /* Send the MM_SET_RF_CONFIG_REQ message to UMAC FW */
    error = rwnx_send_msg(rwnx_hw, rf_config_req, 1, MM_SET_RF_CONFIG_CFM, NULL);

    return (error);
}

extern void get_userconfig_xtal_cap(xtal_cap_conf_t *xtal_cap);

int rwnx_send_rf_calib_req(struct rwnx_hw *rwnx_hw, struct mm_set_rf_calib_cfm *cfm)
{
    struct mm_set_rf_calib_req *rf_calib_req;
	xtal_cap_conf_t xtal_cap = {0,};
    int error;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_SET_RF_CALIB_REQ message */
    rf_calib_req = rwnx_msg_zalloc(MM_SET_RF_CALIB_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_set_rf_calib_req));

    if (!rf_calib_req) {
        return -ENOMEM;
    }

    if(rwnx_hw->usbdev->chipid != PRODUCT_ID_AIC8801){
        rf_calib_req->cal_cfg_24g = 0x0f8f;
    }else{
        rf_calib_req->cal_cfg_24g = 0xbf;
    }
/*	}else if(rwnx_hw->usbdev->chipid != PRODUCT_ID_AIC8800DC ||
			rwnx_hw->usbdev->chipid != PRODUCT_ID_AIC8800DW){
		rf_calib_req->cal_cfg_24g = 0xbf;
	}*/

    rf_calib_req->cal_cfg_5g = 0x3f;
    rf_calib_req->param_alpha = 0x0c34c008;
    rf_calib_req->bt_calib_en = 0;
    rf_calib_req->bt_calib_param = 0x264203;

	get_userconfig_xtal_cap(&xtal_cap);

	if (xtal_cap.enable) {
		AICWFDBG(LOGINFO, "user xtal cap: %d, cap_fine: %d\n", xtal_cap.xtal_cap, xtal_cap.xtal_cap_fine);
		rf_calib_req->xtal_cap = xtal_cap.xtal_cap;
		rf_calib_req->xtal_cap_fine = xtal_cap.xtal_cap_fine;
	} else {
		rf_calib_req->xtal_cap = 0;
		rf_calib_req->xtal_cap_fine = 0;
	}

    /* Send the MM_SET_RF_CALIB_REQ message to UMAC FW */
    error = rwnx_send_msg(rwnx_hw, rf_calib_req, 1, MM_SET_RF_CALIB_CFM, cfm);

    return (error);
};

int rwnx_send_get_macaddr_req(struct rwnx_hw *rwnx_hw, struct mm_get_mac_addr_cfm *cfm)
{
    struct mm_get_mac_addr_req *get_macaddr_req;
    int error;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_GET_MAC_ADDR_REQ message */
    get_macaddr_req = rwnx_msg_zalloc(MM_GET_MAC_ADDR_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_get_mac_addr_req));

    if (!get_macaddr_req) {
        return -ENOMEM;
    }

    get_macaddr_req->get = 1;

    /* Send the MM_GET_MAC_ADDR_REQ  message to UMAC FW */
    error = rwnx_send_msg(rwnx_hw, get_macaddr_req, 1, MM_GET_MAC_ADDR_CFM, cfm);

    return (error);
};


int rwnx_send_get_sta_info_req(struct rwnx_hw *rwnx_hw, u8_l sta_idx, struct mm_get_sta_info_cfm *cfm)
{
	struct mm_get_sta_info_req *get_info_req;
	int error;

	/* Build the MM_GET_STA_INFO_REQ message */
	get_info_req = rwnx_msg_zalloc(MM_GET_STA_INFO_REQ, TASK_MM, DRV_TASK_ID,
						sizeof(struct mm_get_sta_info_req));

	if (!get_info_req) {
		return -ENOMEM;
	}

	get_info_req->sta_idx = sta_idx;

	/* Send the MM_GET_STA_INFO_REQ  message to UMAC FW */
	error = rwnx_send_msg(rwnx_hw, get_info_req, 1, MM_GET_STA_INFO_CFM, cfm);

	return error;
};


#if 0
int rwnx_send_get_sta_txinfo_req(struct rwnx_hw *rwnx_hw, u8_l sta_idx, struct mm_get_sta_txinfo_cfm *cfm)
{
    struct mm_get_sta_txinfo_req *get_txinfo_req;
    int error;


    /* Build the MM_GET_STA_TXINFO_REQ message */
    get_txinfo_req = rwnx_msg_zalloc(MM_GET_STA_TXINFO_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_get_sta_txinfo_req));

    if (!get_txinfo_req) {
        return -ENOMEM;
    }

    get_txinfo_req->sta_idx = 1;

    /* Send the MM_GET_STA_TXINFO_REQ  message to UMAC FW */
    error = rwnx_send_msg(rwnx_hw, get_txinfo_req, 1, MM_GET_STA_TXINFO_CFM, cfm);

    return (error);
}
#endif

int rwnx_send_set_stack_start_req(struct rwnx_hw *rwnx_hw, u8_l on, u8_l efuse_valid, u8_l set_vendor_info,
					u8_l fwtrace_redir_en, struct mm_set_stack_start_cfm *cfm)
{
    struct mm_set_stack_start_req *req;
    int error;

    /* Build the MM_SET_STACK_START_REQ message */
    req = rwnx_msg_zalloc(MM_SET_STACK_START_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_stack_start_req));

    if (!req) {
        return -ENOMEM;
    }

    req->is_stack_start = on;
    req->efuse_valid = efuse_valid;
    req->set_vendor_info = set_vendor_info;
    req->fwtrace_redir = fwtrace_redir_en;
    /* Send the MM_GET_STA_TXINFO_REQ  message to UMAC FW */
    error = rwnx_send_msg(rwnx_hw, req, 1, MM_SET_STACK_START_CFM, cfm);

    return error;
}

#if 0
int rwnx_send_txop_req(struct rwnx_hw *rwnx_hw, uint16_t *txop, u8_l long_nav_en, u8_l cfe_en)
{
    struct mm_set_txop_req *req;
    int error;

    /* Build the MM_SET_TXOP_REQ message */
    req = rwnx_msg_zalloc(MM_SET_TXOP_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_txop_req));

    if (!req) {
            return -ENOMEM;
    }

    req->txop_bk = txop[0];
    req->txop_be = txop[1];
    req->txop_vi = txop[2];
    req->txop_vo = txop[3];
    req->long_nav_en = long_nav_en;
    req->cfe_en = cfe_en;

    /* Send the MM_SET_TXOP_REQ  message to UMAC FW */
    error = rwnx_send_msg(rwnx_hw, req, 1, MM_SET_TXOP_CFM, NULL);

    return error;
}

int rwnx_send_vendor_trx_param_req(struct rwnx_hw *rwnx_hw, uint32_t *edca, uint8_t vif_idx, uint8_t retry_cnt)
{
	struct mm_set_vendor_trx_param_req *req;
	int error;

	RWNX_DBG(RWNX_FN_ENTRY_STR);

	/* Build the MM_SET_VENDOR_TRX_PARAM_REQ message */
    req = rwnx_msg_zalloc(MM_SET_VENDOR_TRX_PARAM_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_vendor_trx_param_req));
	if (!req) {
            return -ENOMEM;
    }

	req->edca[0] = edca[0];
	req->edca[1] = edca[1];
	req->edca[2] =  edca[2];
	req->edca[3] = edca[3];
	req->vif_idx = vif_idx;
	req->retry_cnt = retry_cnt;

	/* Send the MM_SET_VENDOR_TRX_PARAM_REQ message to UMAC FW */
    error = rwnx_send_msg(rwnx_hw, req, 1, MM_SET_VENDOR_TRX_PARAM_CFM, NULL);

	return error;
}

#endif
int rwnx_send_vendor_hwconfig_req(struct rwnx_hw *rwnx_hw, uint32_t hwconfig_id, int32_t *param)
{
	struct mm_set_acs_txop_req *req0;
	struct mm_set_channel_access_req *req1;
    struct mm_set_mac_timescale_req *req2;
    struct mm_set_cca_threshold_req *req3;
    int error;

	switch (hwconfig_id)
	{
	    case ACS_TXOP_REQ:
		/* Build the ACS_TXOP_REQ message */
		req0= rwnx_msg_zalloc(MM_SET_VENDOR_HWCONFIG_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_acs_txop_req) );
		if (!req0)
		    return -ENOMEM;
		req0->hwconfig_id = hwconfig_id;
		req0->txop_be = param[0];
		req0->txop_bk = param[1];
		req0->txop_vi = param[2];
		req0->txop_vo = param[3];
		printk("set_acs_txop_req: be: %x,bk: %x,vi: %x,vo: %x\n",
                        req0->txop_be, req0->txop_bk, req0->txop_vi, req0->txop_vo);
		/* Send the MM_SET_VENDOR_HWCONFIG_CFM  message to UMAC FW */
		error = rwnx_send_msg(rwnx_hw, req0, 1, MM_SET_VENDOR_HWCONFIG_CFM, NULL);
		break;

	    case CHANNEL_ACCESS_REQ:
		/* Build the CHANNEL_ACCESS_REQ message */
		req1 = rwnx_msg_zalloc(MM_SET_VENDOR_HWCONFIG_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_channel_access_req));
		if (!req1)
		    return -ENOMEM;
		req1->hwconfig_id = hwconfig_id;
		req1->edca[0] = param[0];
		req1->edca[1] = param[1];
		req1->edca[2] =	param[2];
		req1->edca[3] = param[3];
		req1->vif_idx = param[4];
		req1->retry_cnt = param[5];
		req1->rts_en = param[6];
		req1->long_nav_en = param[7];
		req1->cfe_en = param[8];
		printk("set_channel_access_req:edca[]= %x %x %x %x\nvif_idx: %x, retry_cnt: %x, rts_en: %x, long_nav_en: %x, cfe_en: %x\n",
			req1->edca[0], req1->edca[1], req1->edca[2], req1->edca[3], req1->vif_idx, req1->retry_cnt, req1->rts_en, req1->long_nav_en, req1->cfe_en);
		/* Send the MM_SET_VENDOR_HWCONFIG_CFM  message to UMAC FW */
		error = rwnx_send_msg(rwnx_hw, req1, 1, MM_SET_VENDOR_HWCONFIG_CFM, NULL);
		break;

	    case MAC_TIMESCALE_REQ:
		/* Build the MAC_TIMESCALE_REQ message */
		req2 = rwnx_msg_zalloc(MM_SET_VENDOR_HWCONFIG_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_mac_timescale_req));
		if (!req2)
		    return -ENOMEM;
		req2->hwconfig_id = hwconfig_id;
		req2->sifsA_time = param[0];
		req2->sifsB_time = param[1];
		req2->slot_time = param[2];
		req2->rx_startdelay_ofdm = param[3];
		req2->rx_startdelay_long = param[4];
		req2->rx_startdelay_short = param[5];
		printk("set_mac_timescale_req:sifsA_time: %x, sifsB_time: %x, slot_time: %x, rx_startdelay ofdm:%x long %x short %x\n",
			req2->sifsA_time, req2->sifsB_time, req2->slot_time, req2->rx_startdelay_ofdm, req2->rx_startdelay_long, req2->rx_startdelay_short);
		/* Send the MM_SET_VENDOR_HWCONFIG_CFM  message to UMAC FW */
		error = rwnx_send_msg(rwnx_hw, req2, 1, MM_SET_VENDOR_HWCONFIG_CFM, NULL);
		break;

	    case CCA_THRESHOLD_REQ:
		/* Build the CCA_THRESHOLD_REQ message */
		req3 = rwnx_msg_zalloc(MM_SET_VENDOR_HWCONFIG_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_cca_threshold_req));
		if (!req3)
		    return -ENOMEM;
		req3->hwconfig_id = hwconfig_id;
	        req3->auto_cca_en = param[0];
		req3->cca20p_rise_th = param[1];
		req3->cca20s_rise_th = param[2];
		req3->cca20p_fall_th = param[3];
		req3->cca20s_fall_th = param[4];
		printk("cca_threshold_req: auto_cca_en:%d\ncca20p_rise_th = %d\ncca20s_rise_th = %d\ncca20p_fall_th = %d\ncca20s_fall_th = %d\n",
			req3->auto_cca_en, req3->cca20p_rise_th, req3->cca20s_rise_th, req3->cca20p_fall_th, req3->cca20s_fall_th);
		/* Send the MM_SET_VENDOR_HWCONFIG_CFM  message to UMAC FW */
		error = rwnx_send_msg(rwnx_hw, req3, 1, MM_SET_VENDOR_HWCONFIG_CFM, NULL);
		break;
	    default:
		return -ENOMEM;
	}
    return error;
}

int rwnx_send_get_fw_version_req(struct rwnx_hw *rwnx_hw, struct mm_get_fw_version_cfm *cfm)
{
    void *req;
    int error;

    /* Build the MM_GET_FW_VERSION_REQ message */
    req = rwnx_msg_zalloc(MM_GET_FW_VERSION_REQ, TASK_MM, DRV_TASK_ID, sizeof(u8));

    if (!req) {
            return -ENOMEM;
    }

    /* Send the MM_GET_FW_VERSION_REQ  message to UMAC FW */
    error = rwnx_send_msg(rwnx_hw, req, 1, MM_GET_FW_VERSION_CFM, cfm);

    return error;
}


extern void get_userconfig_txpwr_idx(txpwr_idx_conf_t *txpwr_idx);

int rwnx_send_txpwr_idx_req(struct rwnx_hw *rwnx_hw)
{
    struct mm_set_txpwr_idx_req *txpwr_idx_req;
    txpwr_idx_conf_t *txpwr_idx;
    int error;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_SET_TXPWR_IDX_REQ message */
    txpwr_idx_req = rwnx_msg_zalloc(MM_SET_TXPWR_IDX_LVL_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_set_txpwr_idx_req));

    if (!txpwr_idx_req) {
        return -ENOMEM;
    }

    txpwr_idx = &txpwr_idx_req->txpwr_idx;
    txpwr_idx->enable = 1;
    txpwr_idx->dsss=9;
    txpwr_idx->ofdmlowrate_2g4=8;
    txpwr_idx->ofdm64qam_2g4=8;
    txpwr_idx->ofdm256qam_2g4=8;
    txpwr_idx->ofdm1024qam_2g4=8;
    txpwr_idx->ofdmlowrate_5g=11;
    txpwr_idx->ofdm64qam_5g=10;
	txpwr_idx->ofdm256qam_5g=9;
	txpwr_idx->ofdm1024qam_5g=9;

	get_userconfig_txpwr_idx(txpwr_idx);

	AICWFDBG(LOGINFO, "%s:enable:%d\r\n", __func__, txpwr_idx->enable);
	AICWFDBG(LOGINFO, "%s:dsss:%d\r\n", __func__, txpwr_idx->dsss);
	AICWFDBG(LOGINFO, "%s:ofdmlowrate_2g4:%d\r\n", __func__, txpwr_idx->ofdmlowrate_2g4);
	AICWFDBG(LOGINFO, "%s:ofdm64qam_2g4:%d\r\n", __func__, txpwr_idx->ofdm64qam_2g4);
	AICWFDBG(LOGINFO, "%s:ofdm256qam_2g4:%d\r\n", __func__, txpwr_idx->ofdm256qam_2g4);
	AICWFDBG(LOGINFO, "%s:ofdm1024qam_2g4:%d\r\n", __func__, txpwr_idx->ofdm1024qam_2g4);
	AICWFDBG(LOGINFO, "%s:ofdmlowrate_5g:%d\r\n", __func__, txpwr_idx->ofdmlowrate_5g);
	AICWFDBG(LOGINFO, "%s:ofdm64qam_5g:%d\r\n", __func__, txpwr_idx->ofdm64qam_5g);
	AICWFDBG(LOGINFO, "%s:ofdm256qam_5g:%d\r\n", __func__, txpwr_idx->ofdm256qam_5g);
	AICWFDBG(LOGINFO, "%s:ofdm1024qam_5g:%d\r\n", __func__, txpwr_idx->ofdm1024qam_5g);

    /* Send the MM_SET_TXPWR_IDX_REQ message to UMAC FW */
    error = rwnx_send_msg(rwnx_hw, txpwr_idx_req, 1, MM_SET_TXPWR_IDX_LVL_CFM, NULL);

    return (error);
}

int rwnx_send_txpwr_lvl_req(struct rwnx_hw *rwnx_hw)
{
    struct mm_set_txpwr_lvl_req *txpwr_lvl_req;
    txpwr_lvl_conf_v2_t txpwr_lvl_v2_tmp;
    txpwr_lvl_conf_v2_t *txpwr_lvl_v2;
    txpwr_loss_conf_t txpwr_loss_tmp;
    txpwr_loss_conf_t *txpwr_loss;
    int error;
    int i;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_SET_TXPWR_LVL_REQ message */
    txpwr_lvl_req = rwnx_msg_zalloc(MM_SET_TXPWR_IDX_LVL_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_set_txpwr_lvl_req));

    if (!txpwr_lvl_req) {
        return -ENOMEM;
    }

    txpwr_lvl_v2 = &txpwr_lvl_v2_tmp;
    txpwr_loss = &txpwr_loss_tmp;
    txpwr_loss->loss_enable = 0;

    get_userconfig_txpwr_lvl_v2_in_fdrv(txpwr_lvl_v2);
    get_userconfig_txpwr_loss(txpwr_loss);
    if (txpwr_lvl_v2->enable == 0) {
        rwnx_msg_free(rwnx_hw, txpwr_lvl_req);
        return 0;
    } else {
        AICWFDBG(LOGINFO, "%s:enable:%d\r\n",               __func__, txpwr_lvl_v2->enable);
        AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_1m_2g4:%d\r\n",  __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[0]);
        AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_2m_2g4:%d\r\n",  __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[1]);
        AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_5m5_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[2]);
        AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_11m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[3]);
        AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_6m_2g4:%d\r\n",  __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[4]);
        AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_9m_2g4:%d\r\n",  __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[5]);
        AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_12m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[6]);
        AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_18m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[7]);
        AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_24m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[8]);
        AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_36m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[9]);
        AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_48m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[10]);
        AICWFDBG(LOGINFO, "%s:lvl_11b_11ag_54m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[11]);
        AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs0_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[0]);
        AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs1_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[1]);
        AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs2_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[2]);
        AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs3_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[3]);
        AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs4_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[4]);
        AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs5_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[5]);
        AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs6_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[6]);
        AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs7_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[7]);
        AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs8_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[8]);
        AICWFDBG(LOGINFO, "%s:lvl_11n_11ac_mcs9_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[9]);
        AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs0_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[0]);
        AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs1_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[1]);
        AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs2_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[2]);
        AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs3_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[3]);
        AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs4_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[4]);
        AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs5_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[5]);
        AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs6_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[6]);
        AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs7_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[7]);
        AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs8_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[8]);
        AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs9_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[9]);
        AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs10_2g4:%d\r\n",   __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[10]);
        AICWFDBG(LOGINFO, "%s:lvl_11ax_mcs11_2g4:%d\r\n",   __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[11]);

    if (txpwr_loss->loss_enable == 1) {
        AICWFDBG(LOGINFO, "%s:loss_value:%d\r\n", __func__, txpwr_loss->loss_value);

        for (i = 0; i <= 11; i++)
            txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[i] += txpwr_loss->loss_value;
        for (i = 0; i <= 9; i++)
            txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[i] += txpwr_loss->loss_value;
        for (i = 0; i <= 11; i++)
            txpwr_lvl_v2->pwrlvl_11ax_2g4[i] += txpwr_loss->loss_value;
    }
        if ((testmode == 0) && (chip_sub_id == 0)) {
            txpwr_lvl_req->txpwr_lvl.enable         = txpwr_lvl_v2->enable;
            txpwr_lvl_req->txpwr_lvl.dsss           = txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[3]; // 11M
            txpwr_lvl_req->txpwr_lvl.ofdmlowrate_2g4= txpwr_lvl_v2->pwrlvl_11ax_2g4[4]; // MCS4
            txpwr_lvl_req->txpwr_lvl.ofdm64qam_2g4  = txpwr_lvl_v2->pwrlvl_11ax_2g4[7]; // MCS7
            txpwr_lvl_req->txpwr_lvl.ofdm256qam_2g4 = txpwr_lvl_v2->pwrlvl_11ax_2g4[9]; // MCS9
            txpwr_lvl_req->txpwr_lvl.ofdm1024qam_2g4= txpwr_lvl_v2->pwrlvl_11ax_2g4[11]; // MCS11
            txpwr_lvl_req->txpwr_lvl.ofdmlowrate_5g = 13; // unused
            txpwr_lvl_req->txpwr_lvl.ofdm64qam_5g   = 13; // unused
            txpwr_lvl_req->txpwr_lvl.ofdm256qam_5g  = 13; // unused
            txpwr_lvl_req->txpwr_lvl.ofdm1024qam_5g = 13; // unused
        } else {
            txpwr_lvl_req->txpwr_lvl_v2 = *txpwr_lvl_v2;
        }

        /* Send the MM_SET_TXPWR_LVL_REQ message to UMAC FW */
        error = rwnx_send_msg(rwnx_hw, txpwr_lvl_req, 1, MM_SET_TXPWR_IDX_LVL_CFM, NULL);

        return (error);
    }
}


extern void get_userconfig_txpwr_ofst(txpwr_ofst_conf_t *txpwr_ofst);

int rwnx_send_txpwr_ofst_req(struct rwnx_hw *rwnx_hw)
{
    struct mm_set_txpwr_ofst_req *txpwr_ofst_req;
    txpwr_ofst_conf_t *txpwr_ofst;
    int error = 0;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_SET_TXPWR_OFST_REQ message */
    txpwr_ofst_req = rwnx_msg_zalloc(MM_SET_TXPWR_OFST_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_set_txpwr_ofst_req));

    if (!txpwr_ofst_req) {
        return -ENOMEM;
    }

    txpwr_ofst = &txpwr_ofst_req->txpwr_ofst;
    txpwr_ofst->enable = 0;
    txpwr_ofst->chan_1_4     = 0;
    txpwr_ofst->chan_5_9     = 0;
    txpwr_ofst->chan_10_13   = 0;
    txpwr_ofst->chan_36_64   = 0;
    txpwr_ofst->chan_100_120 = 0;
    txpwr_ofst->chan_122_140 = 0;
    txpwr_ofst->chan_142_165 = 0;
	if(rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8801){
		get_userconfig_txpwr_ofst(txpwr_ofst);
	}else if(rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DC ||
			rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DW){
		get_userconfig_txpwr_ofst_in_fdrv(txpwr_ofst);
	}
	if(txpwr_ofst->enable){

		AICWFDBG(LOGINFO, "%s:enable:%d\r\n", __func__, txpwr_ofst->enable);
		AICWFDBG(LOGINFO, "%s:chan_1_4:%d\r\n", __func__, txpwr_ofst->chan_1_4);
		AICWFDBG(LOGINFO, "%s:chan_5_9:%d\r\n", __func__, txpwr_ofst->chan_5_9);
		AICWFDBG(LOGINFO, "%s:chan_10_13:%d\r\n", __func__, txpwr_ofst->chan_10_13);
		AICWFDBG(LOGINFO, "%s:chan_36_64:%d\r\n", __func__, txpwr_ofst->chan_36_64);
		AICWFDBG(LOGINFO, "%s:chan_100_120:%d\r\n", __func__, txpwr_ofst->chan_100_120);
		AICWFDBG(LOGINFO, "%s:chan_122_140:%d\r\n", __func__, txpwr_ofst->chan_122_140);
		AICWFDBG(LOGINFO, "%s:chan_142_165:%d\r\n", __func__, txpwr_ofst->chan_142_165);

	    /* Send the MM_SET_TXPWR_OFST_REQ message to UMAC FW */
	    error = rwnx_send_msg(rwnx_hw, txpwr_ofst_req, 1, MM_SET_TXPWR_OFST_CFM, NULL);
	}else{
		AICWFDBG(LOGINFO, "%s:Do not use txpwr_ofst\r\n", __func__);
		rwnx_msg_free(rwnx_hw, txpwr_ofst_req);
	}

    return (error);
}

int rwnx_send_set_filter(struct rwnx_hw *rwnx_hw, uint32_t filter)
{
    struct mm_set_filter_req *set_filter_req_param;
    uint32_t rx_filter = 0;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_SET_FILTER_REQ message */
    set_filter_req_param =
        rwnx_msg_zalloc(MM_SET_FILTER_REQ, TASK_MM, DRV_TASK_ID,
                        sizeof(struct mm_set_filter_req));
    if (!set_filter_req_param)
        return -ENOMEM;

    /* Set parameters for the MM_SET_FILTER_REQ message */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0)
    if (filter & FIF_PROMISC_IN_BSS)
        rx_filter |= NXMAC_ACCEPT_UNICAST_BIT;
#endif
    if (filter & FIF_ALLMULTI)
        rx_filter |= NXMAC_ACCEPT_MULTICAST_BIT;

    if (filter & (FIF_FCSFAIL | FIF_PLCPFAIL))
        rx_filter |= NXMAC_ACCEPT_ERROR_FRAMES_BIT;

    if (filter & FIF_BCN_PRBRESP_PROMISC)
        rx_filter |= NXMAC_ACCEPT_OTHER_BSSID_BIT;

    if (filter & FIF_CONTROL)
        rx_filter |= NXMAC_ACCEPT_OTHER_CNTRL_FRAMES_BIT |
                     NXMAC_ACCEPT_CF_END_BIT |
                     NXMAC_ACCEPT_ACK_BIT |
                     NXMAC_ACCEPT_CTS_BIT |
                     NXMAC_ACCEPT_RTS_BIT |
                     NXMAC_ACCEPT_BA_BIT | NXMAC_ACCEPT_BAR_BIT;

    if (filter & FIF_OTHER_BSS)
        rx_filter |= NXMAC_ACCEPT_OTHER_BSSID_BIT;

    if (filter & FIF_PSPOLL) {
        /* TODO: check if the MAC filters apply to our BSSID or is general */
        rx_filter |= NXMAC_ACCEPT_PS_POLL_BIT;
    }

    if (filter & FIF_PROBE_REQ) {
        rx_filter |= NXMAC_ACCEPT_PROBE_REQ_BIT;
        rx_filter |= NXMAC_ACCEPT_ALL_BEACON_BIT;
    }

    /* Add the filter flags that are set by default and cannot be changed here */
    rx_filter |= RWNX_MAC80211_NOT_CHANGEABLE;

    /* XXX */
    //if (ieee80211_hw_check(rwnx_hw->hw, AMPDU_AGGREGATION))
        rx_filter |= NXMAC_ACCEPT_BA_BIT;

    /* Now copy all the flags into the message parameter */
    set_filter_req_param->filter = rx_filter;

    RWNX_DBG("new total_flags = 0x%08x\nrx filter set to  0x%08x\n",
             filter, rx_filter);

    /* Send the MM_SET_FILTER_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, set_filter_req_param, 1, MM_SET_FILTER_CFM, NULL);
}

/******************************************************************************
 *    Control messages handling functions (FULLMAC only)
 *****************************************************************************/
#ifdef CONFIG_RWNX_FULLMAC
#ifdef CONFIG_HE_FOR_OLD_KERNEL
extern struct ieee80211_sband_iftype_data rwnx_he_capa;
#endif
#ifdef CONFIG_VHT_FOR_OLD_KERNEL
static struct ieee80211_sta_vht_cap* rwnx_vht_capa;
#endif
int rwnx_send_me_config_req(struct rwnx_hw *rwnx_hw)
{
    struct me_config_req *req;
    struct wiphy *wiphy = rwnx_hw->wiphy;

    //#ifdef USE_5G
    //struct ieee80211_sta_ht_cap *ht_cap = &wiphy->bands[NL80211_BAND_5GHZ]->ht_cap;
    //struct ieee80211_sta_vht_cap *vht_cap = &wiphy->bands[NL80211_BAND_5GHZ]->vht_cap;
    //#else
    //struct ieee80211_sta_ht_cap *ht_cap = &wiphy->bands[NL80211_BAND_2GHZ]->ht_cap;
    //struct ieee80211_sta_vht_cap *vht_cap = &wiphy->bands[NL80211_BAND_2GHZ]->vht_cap;
	//#endif
	struct ieee80211_sta_ht_cap *ht_cap;
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0) || defined(CONFIG_VHT_FOR_OLD_KERNEL)
	struct ieee80211_sta_vht_cap *vht_cap;
    #endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
    struct ieee80211_sta_he_cap const *he_cap;
#else
    #ifdef CONFIG_HE_FOR_OLD_KERNEL
    struct ieee80211_sta_he_cap const *he_cap;
    #endif
#endif
    //uint8_t *ht_mcs = (uint8_t *)&ht_cap->mcs;
    uint8_t *ht_mcs;
    int i;

	if (rwnx_hw->band_5g_support) {
		ht_cap = &wiphy->bands[NL80211_BAND_5GHZ]->ht_cap;
        #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0) || defined(CONFIG_VHT_FOR_OLD_KERNEL)
		vht_cap = &wiphy->bands[NL80211_BAND_5GHZ]->vht_cap;
        #endif
	} else {
		ht_cap = &wiphy->bands[NL80211_BAND_2GHZ]->ht_cap;
        #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0) || defined(CONFIG_VHT_FOR_OLD_KERNEL)
		vht_cap = &wiphy->bands[NL80211_BAND_2GHZ]->vht_cap;
        #endif
	}
    #ifdef CONFIG_VHT_FOR_OLD_KERNEL
    rwnx_vht_capa = vht_cap;
    #endif

	ht_mcs = (uint8_t *)&ht_cap->mcs;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the ME_CONFIG_REQ message */
    req = rwnx_msg_zalloc(ME_CONFIG_REQ, TASK_ME, DRV_TASK_ID,
                                   sizeof(struct me_config_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the ME_CONFIG_REQ message */
    req->ht_supp = ht_cap->ht_supported;
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0) || defined(CONFIG_VHT_FOR_OLD_KERNEL)
    req->vht_supp = vht_cap->vht_supported;
    #endif
    req->ht_cap.ht_capa_info = cpu_to_le16(ht_cap->cap | IEEE80211_HT_CAP_LDPC_CODING);
    req->ht_cap.a_mpdu_param = ht_cap->ampdu_factor |
                                     (ht_cap->ampdu_density <<
                                         IEEE80211_HT_AMPDU_PARM_DENSITY_SHIFT);
    for (i = 0; i < sizeof(ht_cap->mcs); i++)
        req->ht_cap.mcs_rate[i] = ht_mcs[i];
    req->ht_cap.ht_extended_capa = 0;
    req->ht_cap.tx_beamforming_capa = 0;
    req->ht_cap.asel_capa = 0;

    #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0) || defined(CONFIG_VHT_FOR_OLD_KERNEL)
    if(req->vht_supp) {
    	req->vht_cap.vht_capa_info = cpu_to_le32(vht_cap->cap);
    	req->vht_cap.rx_highest = cpu_to_le16(vht_cap->vht_mcs.rx_highest);
    	req->vht_cap.rx_mcs_map = cpu_to_le16(vht_cap->vht_mcs.rx_mcs_map);
    	req->vht_cap.tx_highest = cpu_to_le16(vht_cap->vht_mcs.tx_highest);
    	req->vht_cap.tx_mcs_map = cpu_to_le16(vht_cap->vht_mcs.tx_mcs_map);
    }
    #endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0) || defined(CONFIG_HE_FOR_OLD_KERNEL)
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
    if (wiphy->bands[NL80211_BAND_2GHZ]->iftype_data != NULL) {
        he_cap = &wiphy->bands[NL80211_BAND_2GHZ]->iftype_data->he_cap;
    #endif
    #if defined(CONFIG_HE_FOR_OLD_KERNEL)
    if (1) {
        he_cap = &rwnx_he_capa.he_cap;
    #endif

		req->he_supp = he_cap->has_he;

		for (i = 0; i < ARRAY_SIZE(he_cap->he_cap_elem.mac_cap_info); i++) {
			req->he_cap.mac_cap_info[i] = he_cap->he_cap_elem.mac_cap_info[i];
		}
		for (i = 0; i < ARRAY_SIZE(he_cap->he_cap_elem.phy_cap_info); i++) {
			req->he_cap.phy_cap_info[i] = he_cap->he_cap_elem.phy_cap_info[i];
		}
		req->he_cap.mcs_supp.rx_mcs_80 = cpu_to_le16(he_cap->he_mcs_nss_supp.rx_mcs_80);
		req->he_cap.mcs_supp.tx_mcs_80 = cpu_to_le16(he_cap->he_mcs_nss_supp.tx_mcs_80);
		req->he_cap.mcs_supp.rx_mcs_160 = cpu_to_le16(he_cap->he_mcs_nss_supp.rx_mcs_160);
		req->he_cap.mcs_supp.tx_mcs_160 = cpu_to_le16(he_cap->he_mcs_nss_supp.tx_mcs_160);
		req->he_cap.mcs_supp.rx_mcs_80p80 = cpu_to_le16(he_cap->he_mcs_nss_supp.rx_mcs_80p80);
		req->he_cap.mcs_supp.tx_mcs_80p80 = cpu_to_le16(he_cap->he_mcs_nss_supp.tx_mcs_80p80);
		for (i = 0; i < MAC_HE_PPE_THRES_MAX_LEN; i++) {
			req->he_cap.ppe_thres[i] = he_cap->ppe_thres[i];
		}
		req->he_ul_on = rwnx_hw->mod_params->he_ul_on;
	}

#else
    req->he_supp = false;
    req->he_ul_on = false;
#endif
    req->ps_on = rwnx_hw->mod_params->ps_on;
    req->dpsm = rwnx_hw->mod_params->dpsm;
    req->tx_lft = rwnx_hw->mod_params->tx_lft;
    req->ant_div_on = rwnx_hw->mod_params->ant_div;

    if (rwnx_hw->mod_params->use_80){
        req->phy_bw_max = PHY_CHNL_BW_80;
	}else if (rwnx_hw->mod_params->use_2040){
		req->phy_bw_max = PHY_CHNL_BW_40;
	}else{
		req->phy_bw_max = PHY_CHNL_BW_20;
	}

#if 0
	if(!rwnx_hw->he_flag){
		req->he_supp = false;
		req->he_ul_on = false;
	}

    wiphy_info(wiphy, "HT supp %d, VHT supp %d, HE supp %d\n", req->ht_supp,
                                                               req->vht_supp,
                                                               req->he_supp);
#endif

	AICWFDBG(LOGINFO, "HT supp %d, VHT supp %d, HE supp %d\n", req->ht_supp,
                                                               req->vht_supp,
                                                               req->he_supp);

	/* Send the ME_CONFIG_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, ME_CONFIG_CFM, NULL);
}
int rwnx_send_me_chan_config_req(struct rwnx_hw *rwnx_hw)
{
    struct me_chan_config_req *req;
    struct wiphy *wiphy = rwnx_hw->wiphy;
    int i;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the ME_CHAN_CONFIG_REQ message */
    req = rwnx_msg_zalloc(ME_CHAN_CONFIG_REQ, TASK_ME, DRV_TASK_ID,
                                            sizeof(struct me_chan_config_req));
    if (!req)
        return -ENOMEM;

    req->chan2G4_cnt=  0;
    if (wiphy->bands[NL80211_BAND_2GHZ] != NULL) {
        struct ieee80211_supported_band *b = wiphy->bands[NL80211_BAND_2GHZ];
        for (i = 0; i < b->n_channels; i++) {
            req->chan2G4[req->chan2G4_cnt].flags = 0;
            if (b->channels[i].flags & IEEE80211_CHAN_DISABLED)
                req->chan2G4[req->chan2G4_cnt].flags |= CHAN_DISABLED;
            req->chan2G4[req->chan2G4_cnt].flags |= get_chan_flags(b->channels[i].flags);
            req->chan2G4[req->chan2G4_cnt].band = NL80211_BAND_2GHZ;
            req->chan2G4[req->chan2G4_cnt].freq = b->channels[i].center_freq;
            req->chan2G4[req->chan2G4_cnt].tx_power = chan_to_fw_pwr(b->channels[i].max_power);
            req->chan2G4_cnt++;
            if (req->chan2G4_cnt == MAC_DOMAINCHANNEL_24G_MAX)
                break;
        }
    }

    req->chan5G_cnt = 0;
    if (wiphy->bands[NL80211_BAND_5GHZ] != NULL) {
        struct ieee80211_supported_band *b = wiphy->bands[NL80211_BAND_5GHZ];
        for (i = 0; i < b->n_channels; i++) {
            req->chan5G[req->chan5G_cnt].flags = 0;
            if (b->channels[i].flags & IEEE80211_CHAN_DISABLED)
                req->chan5G[req->chan5G_cnt].flags |= CHAN_DISABLED;
            req->chan5G[req->chan5G_cnt].flags |= get_chan_flags(b->channels[i].flags);
            req->chan5G[req->chan5G_cnt].band = NL80211_BAND_5GHZ;
            req->chan5G[req->chan5G_cnt].freq = b->channels[i].center_freq;
            req->chan5G[req->chan5G_cnt].tx_power = chan_to_fw_pwr(b->channels[i].max_power);
            req->chan5G_cnt++;
            if (req->chan5G_cnt == MAC_DOMAINCHANNEL_5G_MAX)
                break;
        }
    }

    /* Send the ME_CHAN_CONFIG_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, ME_CHAN_CONFIG_CFM, NULL);
}

int rwnx_send_me_set_control_port_req(struct rwnx_hw *rwnx_hw, bool opened, u8 sta_idx)
{
    struct me_set_control_port_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the ME_SET_CONTROL_PORT_REQ message */
    req = rwnx_msg_zalloc(ME_SET_CONTROL_PORT_REQ, TASK_ME, DRV_TASK_ID,
                                   sizeof(struct me_set_control_port_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the ME_SET_CONTROL_PORT_REQ message */
    req->sta_idx = sta_idx;
    req->control_port_open = opened;

    /* Send the ME_SET_CONTROL_PORT_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, ME_SET_CONTROL_PORT_CFM, NULL);
}

int rwnx_send_me_sta_add(struct rwnx_hw *rwnx_hw, struct station_parameters *params,
                         const u8 *mac, u8 inst_nbr, struct me_sta_add_cfm *cfm)
{
    struct me_sta_add_req *req;
    u8 *ht_mcs = (u8 *)&params->ht_capa->mcs;
    int i;
    struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[inst_nbr];
    #if (defined CONFIG_HE_FOR_OLD_KERNEL) || (defined CONFIG_VHT_FOR_OLD_KERNEL)
    struct aic_sta *sta = &rwnx_hw->aic_table[rwnx_vif->ap.aic_index];
    printk("assoc_req idx %d, he: %d, vht: %d\n ", rwnx_vif->ap.aic_index, sta->he, sta->vht);
    if (rwnx_vif->ap.aic_index < NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX)
        rwnx_vif->ap.aic_index++;
    else
        rwnx_vif->ap.aic_index = 0;
    #endif
    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_STA_ADD_REQ message */
    req = rwnx_msg_zalloc(ME_STA_ADD_REQ, TASK_ME, DRV_TASK_ID,
                                  sizeof(struct me_sta_add_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the MM_STA_ADD_REQ message */
    memcpy(&(req->mac_addr.array[0]), mac, ETH_ALEN);

    req->rate_set.length = params->supported_rates_len;
    for (i = 0; i < params->supported_rates_len; i++)
        req->rate_set.array[i] = params->supported_rates[i];

    req->flags = 0;
    if (params->ht_capa) {
        const struct ieee80211_ht_cap *ht_capa = params->ht_capa;

        req->flags |= STA_HT_CAPA;
        req->ht_cap.ht_capa_info = cpu_to_le16(ht_capa->cap_info);
        req->ht_cap.a_mpdu_param = ht_capa->ampdu_params_info;
        for (i = 0; i < sizeof(ht_capa->mcs); i++)
            req->ht_cap.mcs_rate[i] = ht_mcs[i];
        req->ht_cap.ht_extended_capa = cpu_to_le16(ht_capa->extended_ht_cap_info);
        req->ht_cap.tx_beamforming_capa = cpu_to_le32(ht_capa->tx_BF_cap_info);
        req->ht_cap.asel_capa = ht_capa->antenna_selection_info;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
    if (params->vht_capa) {
        const struct ieee80211_vht_cap *vht_capa = params->vht_capa;

        req->flags |= STA_VHT_CAPA;
        req->vht_cap.vht_capa_info = cpu_to_le32(vht_capa->vht_cap_info);
        req->vht_cap.rx_highest = cpu_to_le16(vht_capa->supp_mcs.rx_highest);
        req->vht_cap.rx_mcs_map = cpu_to_le16(vht_capa->supp_mcs.rx_mcs_map);
        req->vht_cap.tx_highest = cpu_to_le16(vht_capa->supp_mcs.tx_highest);
        req->vht_cap.tx_mcs_map = cpu_to_le16(vht_capa->supp_mcs.tx_mcs_map);
    }
#elif defined(CONFIG_VHT_FOR_OLD_KERNEL)
    if (sta->vht) {
        const struct ieee80211_vht_cap *vht_capa = rwnx_vht_capa;

        req->flags |= STA_VHT_CAPA;
        req->vht_cap.vht_capa_info = cpu_to_le32(vht_capa->vht_cap_info);
        req->vht_cap.rx_highest = cpu_to_le16(vht_capa->supp_mcs.rx_highest);
        req->vht_cap.rx_mcs_map = cpu_to_le16(vht_capa->supp_mcs.rx_mcs_map);
        req->vht_cap.tx_highest = cpu_to_le16(vht_capa->supp_mcs.tx_highest);
        req->vht_cap.tx_mcs_map = cpu_to_le16(vht_capa->supp_mcs.tx_mcs_map);
    }
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
    if (params->he_capa) {
        const struct ieee80211_he_cap_elem *he_capa = params->he_capa;
        struct ieee80211_he_mcs_nss_supp *mcs_nss_supp =
                                (struct ieee80211_he_mcs_nss_supp *)(he_capa + 1);

        req->flags |= STA_HE_CAPA;
        for (i = 0; i < ARRAY_SIZE(he_capa->mac_cap_info); i++) {
            req->he_cap.mac_cap_info[i] = he_capa->mac_cap_info[i];
        }
        for (i = 0; i < ARRAY_SIZE(he_capa->phy_cap_info); i++) {
            req->he_cap.phy_cap_info[i] = he_capa->phy_cap_info[i];
        }
        req->he_cap.mcs_supp.rx_mcs_80 = mcs_nss_supp->rx_mcs_80;
        req->he_cap.mcs_supp.tx_mcs_80 = mcs_nss_supp->tx_mcs_80;
        req->he_cap.mcs_supp.rx_mcs_160 = mcs_nss_supp->rx_mcs_160;
        req->he_cap.mcs_supp.tx_mcs_160 = mcs_nss_supp->tx_mcs_160;
        req->he_cap.mcs_supp.rx_mcs_80p80 = mcs_nss_supp->rx_mcs_80p80;
        req->he_cap.mcs_supp.tx_mcs_80p80 = mcs_nss_supp->tx_mcs_80p80;
    }
#else
	#ifdef CONFIG_HE_FOR_OLD_KERNEL
	if (sta->he) {
		const struct ieee80211_he_cap_elem *he_capa = &rwnx_he_capa.he_cap.he_cap_elem;
		struct ieee80211_he_mcs_nss_supp *mcs_nss_supp =
								(struct ieee80211_he_mcs_nss_supp *)(he_capa + 1);
		req->flags |= STA_HE_CAPA;
		for (i = 0; i < ARRAY_SIZE(he_capa->mac_cap_info); i++) {
			req->he_cap.mac_cap_info[i] = he_capa->mac_cap_info[i];
		}
		for (i = 0; i < ARRAY_SIZE(he_capa->phy_cap_info); i++) {
			req->he_cap.phy_cap_info[i] = he_capa->phy_cap_info[i];
		}
		req->he_cap.mcs_supp.rx_mcs_80 = mcs_nss_supp->rx_mcs_80;
		req->he_cap.mcs_supp.tx_mcs_80 = mcs_nss_supp->tx_mcs_80;
		req->he_cap.mcs_supp.rx_mcs_160 = mcs_nss_supp->rx_mcs_160;
		req->he_cap.mcs_supp.tx_mcs_160 = mcs_nss_supp->tx_mcs_160;
		req->he_cap.mcs_supp.rx_mcs_80p80 = mcs_nss_supp->rx_mcs_80p80;
		req->he_cap.mcs_supp.tx_mcs_80p80 = mcs_nss_supp->tx_mcs_80p80;
    }
	#endif
#endif

    if (params->sta_flags_set & BIT(NL80211_STA_FLAG_WME))
        req->flags |= STA_QOS_CAPA;

    if (params->sta_flags_set & BIT(NL80211_STA_FLAG_MFP))
        req->flags |= STA_MFP_CAPA;

    #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
    if (params->opmode_notif_used) {
        req->flags |= STA_OPMOD_NOTIF;
        req->opmode = params->opmode_notif;
    }
    #endif

    req->aid = cpu_to_le16(params->aid);
    req->uapsd_queues = params->uapsd_queues;
    req->max_sp_len = params->max_sp * 2;
    req->vif_idx = inst_nbr;

    if (params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER)) {
        //struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[inst_nbr];
        req->tdls_sta = true;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
        if ((params->ext_capab[3] & WLAN_EXT_CAPA4_TDLS_CHAN_SWITCH) &&
            !rwnx_vif->tdls_chsw_prohibited)
            req->tdls_chsw_allowed = true;
#endif
        if (rwnx_vif->tdls_status == TDLS_SETUP_RSP_TX)
            req->tdls_sta_initiator = true;
    }

    /* Send the ME_STA_ADD_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, ME_STA_ADD_CFM, cfm);
}

int rwnx_send_me_sta_del(struct rwnx_hw *rwnx_hw, u8 sta_idx, bool tdls_sta)
{
    struct me_sta_del_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_STA_DEL_REQ message */
    req = rwnx_msg_zalloc(ME_STA_DEL_REQ, TASK_ME, DRV_TASK_ID,
                          sizeof(struct me_sta_del_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the MM_STA_DEL_REQ message */
    req->sta_idx = sta_idx;
    req->tdls_sta = tdls_sta;

    /* Send the ME_STA_DEL_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, ME_STA_DEL_CFM, NULL);
}

int rwnx_send_me_traffic_ind(struct rwnx_hw *rwnx_hw, u8 sta_idx, bool uapsd, u8 tx_status)
{
    struct me_traffic_ind_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the ME_UTRAFFIC_IND_REQ message */
    req = rwnx_msg_zalloc(ME_TRAFFIC_IND_REQ, TASK_ME, DRV_TASK_ID,
                          sizeof(struct me_traffic_ind_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the ME_TRAFFIC_IND_REQ message */
    req->sta_idx = sta_idx;
    req->tx_avail = tx_status;
    req->uapsd = uapsd;

    /* Send the ME_TRAFFIC_IND_REQ to UMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, ME_TRAFFIC_IND_CFM, NULL);
}

int rwnx_send_me_rc_stats(struct rwnx_hw *rwnx_hw,
                          u8 sta_idx,
                          struct me_rc_stats_cfm *cfm)
{
    struct me_rc_stats_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the ME_RC_STATS_REQ message */
    req = rwnx_msg_zalloc(ME_RC_STATS_REQ, TASK_ME, DRV_TASK_ID,
                                  sizeof(struct me_rc_stats_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the ME_RC_STATS_REQ message */
    req->sta_idx = sta_idx;

    /* Send the ME_RC_STATS_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, ME_RC_STATS_CFM, cfm);
}

int rwnx_send_me_rc_set_rate(struct rwnx_hw *rwnx_hw,
                             u8 sta_idx,
                             u16 rate_cfg)
{
    struct me_rc_set_rate_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the ME_RC_SET_RATE_REQ message */
    req = rwnx_msg_zalloc(ME_RC_SET_RATE_REQ, TASK_ME, DRV_TASK_ID,
                          sizeof(struct me_rc_set_rate_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the ME_RC_SET_RATE_REQ message */
    req->sta_idx = sta_idx;
    req->fixed_rate_cfg = rate_cfg;

    /* Send the ME_RC_SET_RATE_REQ message to FW */
    return rwnx_send_msg(rwnx_hw, req, 0, 0, NULL);
}

int rwnx_send_me_set_ps_mode(struct rwnx_hw *rwnx_hw, u8 ps_mode)
{
    struct me_set_ps_mode_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the ME_SET_PS_MODE_REQ message */
    req = rwnx_msg_zalloc(ME_SET_PS_MODE_REQ, TASK_ME, DRV_TASK_ID,
                          sizeof(struct me_set_ps_mode_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the ME_SET_PS_MODE_REQ message */
    req->ps_state = ps_mode;

    /* Send the ME_SET_PS_MODE_REQ message to FW */
    return rwnx_send_msg(rwnx_hw, req, 1, ME_SET_PS_MODE_CFM, NULL);
}

int rwnx_send_me_set_lp_level(struct rwnx_hw *rwnx_hw, u8 lp_level)
{
    struct me_set_lp_level_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the ME_SET_LP_LEVEL_REQ message */
    req = rwnx_msg_zalloc(ME_SET_LP_LEVEL_REQ, TASK_ME, DRV_TASK_ID,
                          sizeof(struct me_set_lp_level_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the ME_SET_LP_LEVEL_REQ message */
    req->lp_level = lp_level;

    /* Send the ME_SET_LP_LEVEL_REQ message to FW */
    return rwnx_send_msg(rwnx_hw, req, 1, ME_SET_LP_LEVEL_CFM, NULL);
}

int rwnx_send_sm_connect_req(struct rwnx_hw *rwnx_hw,
                             struct rwnx_vif *rwnx_vif,
                             struct cfg80211_connect_params *sme,
                             struct sm_connect_cfm *cfm)
{
    struct sm_connect_req *req;
    int i;
    u32_l flags = 0;
    bool gval = false;
    bool pval = false;
	
    rwnx_vif->wep_enabled = false;
    rwnx_vif->wep_auth_err = false;
    rwnx_vif->last_auth_type = 0;
	

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the SM_CONNECT_REQ message */
    req = rwnx_msg_zalloc(SM_CONNECT_REQ, TASK_SM, DRV_TASK_ID,
                                   sizeof(struct sm_connect_req));
    if (!req)
        return -ENOMEM;

    if ((sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP40) ||
        (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP104)) {
         gval = true;
    }

    if (sme->crypto.n_ciphers_pairwise &&
        ((sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_WEP40) ||
         (sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_WEP104))) {
        pval = true;
    }

    /* Set parameters for the SM_CONNECT_REQ message */
    if (sme->crypto.n_ciphers_pairwise &&
        ((sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_WEP40) ||
         (sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_TKIP) ||
         (sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_WEP104)))
        flags |= DISABLE_HT;

    if (sme->crypto.control_port)
        flags |= CONTROL_PORT_HOST;

    if (sme->crypto.control_port_no_encrypt)
        flags |= CONTROL_PORT_NO_ENC;

    if (use_pairwise_key(&sme->crypto))
        flags |= WPA_WPA2_IN_USE;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0) || defined (CONFIG_WPA3_FOR_OLD_KERNEL)
    if (sme->mfp == NL80211_MFP_REQUIRED)
        flags |= MFP_IN_USE;
#endif
    if (rwnx_vif->sta.ap)
        flags |= REASSOCIATION;

    req->ctrl_port_ethertype = sme->crypto.control_port_ethertype;

    if (sme->bssid)
        memcpy(&req->bssid, sme->bssid, ETH_ALEN);
    else
        req->bssid = mac_addr_bcst;
    req->vif_idx = rwnx_vif->vif_index;
    if (sme->channel) {
        req->chan.band = sme->channel->band;
        req->chan.freq = sme->channel->center_freq;
        req->chan.flags = get_chan_flags(sme->channel->flags);
    } else {
        req->chan.freq = (u16_l)-1;
    }
    for (i = 0; i < sme->ssid_len; i++)
        req->ssid.array[i] = sme->ssid[i];
    req->ssid.length = sme->ssid_len;

    req->flags = flags;
    if (WARN_ON(sme->ie_len > sizeof(req->ie_buf)))
        goto invalid_param;
    if (sme->ie_len)
        memcpy(req->ie_buf, sme->ie, sme->ie_len);
    req->ie_len = sme->ie_len;
    req->listen_interval = rwnx_mod_params.listen_itv;
    req->dont_wait_bcmc = !rwnx_mod_params.listen_bcmc;

    /* Set auth_type */
    if (sme->auth_type == NL80211_AUTHTYPE_AUTOMATIC)
        req->auth_type = WLAN_AUTH_OPEN;
    else if (sme->auth_type == NL80211_AUTHTYPE_OPEN_SYSTEM)
        req->auth_type = WLAN_AUTH_OPEN;
    else if (sme->auth_type == NL80211_AUTHTYPE_SHARED_KEY)
        req->auth_type = WLAN_AUTH_SHARED_KEY;
    else if (sme->auth_type == NL80211_AUTHTYPE_FT)
        req->auth_type = WLAN_AUTH_FT;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0) || defined (CONFIG_WPA3_FOR_OLD_KERNEL)
    else if (sme->auth_type == NL80211_AUTHTYPE_SAE)
        req->auth_type = WLAN_AUTH_SAE;
#endif
    else
        goto invalid_param;

    /* Set UAPSD queues */
    req->uapsd_queues = rwnx_mod_params.uapsd_queues;

    rwnx_vif->wep_enabled = pval & gval;

    if (rwnx_vif->wep_enabled) {
        rwnx_vif->last_auth_type = sme->auth_type;
    }
#ifdef CONFIG_USB_WIRELESS_EXT
	memset(rwnx_hw->wext_essid, 0, 32);
	memcpy(rwnx_hw->wext_essid, sme->ssid, (int)sme->ssid_len);
#endif

	rwnx_vif->sta.ssid_len = (int)sme->ssid_len;
	memset(rwnx_vif->sta.ssid, 0, rwnx_vif->sta.ssid_len + 1);
	memcpy(rwnx_vif->sta.ssid, sme->ssid, rwnx_vif->sta.ssid_len);
	memcpy(rwnx_vif->sta.bssid, sme->bssid, ETH_ALEN);

	AICWFDBG(LOGINFO, "%s drv_vif_index:%d connect to %s(%d) channel:%d auth_type:%d\r\n",
		__func__,
		rwnx_vif->drv_vif_index,
		rwnx_vif->sta.ssid,
		rwnx_vif->sta.ssid_len,
		req->chan.freq,
		req->auth_type);

    /* Send the SM_CONNECT_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, SM_CONNECT_CFM, cfm);

invalid_param:
    rwnx_msg_free(rwnx_hw, req);
    return -EINVAL;
}

int rwnx_send_sm_disconnect_req(struct rwnx_hw *rwnx_hw,
                                struct rwnx_vif *rwnx_vif,
                                u16 reason)
{
    struct sm_disconnect_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the SM_DISCONNECT_REQ message */
    req = rwnx_msg_zalloc(SM_DISCONNECT_REQ, TASK_SM, DRV_TASK_ID,
                                   sizeof(struct sm_disconnect_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the SM_DISCONNECT_REQ message */
    req->reason_code = reason;
    req->vif_idx = rwnx_vif->vif_index;

    /* Send the SM_DISCONNECT_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, SM_DISCONNECT_CFM, NULL);
}

int rwnx_send_sm_external_auth_required_rsp(struct rwnx_hw *rwnx_hw,
                                            struct rwnx_vif *rwnx_vif,
                                            u16 status)
{
    struct sm_external_auth_required_rsp *rsp;

    /* Build the SM_EXTERNAL_AUTH_CFM message */
    rsp = rwnx_msg_zalloc(SM_EXTERNAL_AUTH_REQUIRED_RSP, TASK_SM, DRV_TASK_ID,
                          sizeof(struct sm_external_auth_required_rsp));
    if (!rsp)
        return -ENOMEM;

    rsp->status = status;
    rsp->vif_idx = rwnx_vif->vif_index;

    /* send the SM_EXTERNAL_AUTH_REQUIRED_RSP message UMAC FW */
    return rwnx_send_msg(rwnx_hw, rsp, 0, 0, NULL);
}

int rwnx_send_apm_start_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
                            struct cfg80211_ap_settings *settings,
                            struct apm_start_cfm *cfm,
                            struct rwnx_ipc_elem_var *elem)
{
    struct apm_start_req *req;
    struct rwnx_bcn *bcn = &vif->ap.bcn;
    u8 *buf;
    u32 flags = 0;
    const u8 *rate_ie;
    u8 rate_len = 0;
    int var_offset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
    const u8 *var_pos;
    int len, i;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the APM_START_REQ message */
    req = rwnx_msg_zalloc(APM_START_REQ, TASK_APM, DRV_TASK_ID,
                                   sizeof(struct apm_start_req));
    if (!req)
        return -ENOMEM;

    // Build the beacon
    bcn->dtim = (u8)settings->dtim_period;
    buf = rwnx_build_bcn(bcn, &settings->beacon);
    if (!buf) {
        rwnx_msg_free(rwnx_hw, req);
        return -ENOMEM;
    }

    // Retrieve the basic rate set from the beacon buffer
    len = bcn->len - var_offset;
    var_pos = buf + var_offset;

// Assume that rate higher that 54 Mbps are BSS membership
#define IS_BASIC_RATE(r) (r & 0x80) && ((r & ~0x80) <= (54 * 2))

    rate_ie = cfg80211_find_ie(WLAN_EID_SUPP_RATES, var_pos, len);
    if (rate_ie) {
        const u8 *rates = rate_ie + 2;
        for (i = 0; (i < rate_ie[1]) && (rate_len < MAC_RATESET_LEN); i++) {
            if (IS_BASIC_RATE(rates[i]))
                req->basic_rates.array[rate_len++] = rates[i];
        }
    }
    rate_ie = cfg80211_find_ie(WLAN_EID_EXT_SUPP_RATES, var_pos, len);
    if (rate_ie) {
        const u8 *rates = rate_ie + 2;
        for (i = 0; (i < rate_ie[1]) && (rate_len < MAC_RATESET_LEN); i++) {
            if (IS_BASIC_RATE(rates[i]))
                req->basic_rates.array[rate_len++] = rates[i];
        }
    }
    req->basic_rates.length = rate_len;
#undef IS_BASIC_RATE

    #if 0
    // Sync buffer for FW
    if ((error = rwnx_ipc_elem_var_allocs(rwnx_hw, elem, bcn->len,
                                          DMA_TO_DEVICE, buf, NULL, NULL))) {
        return error;
    }
    #else
    rwnx_send_bcn(rwnx_hw, buf, vif->vif_index, bcn->len);
    #endif

    /* Set parameters for the APM_START_REQ message */
    req->vif_idx = vif->vif_index;
    req->bcn_addr = elem->dma_addr;
    req->bcn_len = bcn->len;
    req->tim_oft = bcn->head_len;
    req->tim_len = bcn->tim_len;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
    req->chan.band = settings->chandef.chan->band;
    req->chan.freq = settings->chandef.chan->center_freq;
#endif
    req->chan.flags = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
    req->chan.tx_power = chan_to_fw_pwr(settings->chandef.chan->max_power);
    req->center_freq1 = settings->chandef.center_freq1;
    req->center_freq2 = settings->chandef.center_freq2;
    req->ch_width = bw2chnl[settings->chandef.width];
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
    req->chan.band = rwnx_hw->ap_chan.band;
    req->chan.freq = rwnx_hw->ap_chan.prim20_freq;
    req->center_freq1 = rwnx_hw->ap_chan.center1_freq;
    req->center_freq2 = rwnx_hw->ap_chan.center2_freq;
    req->chan.tx_power = rwnx_hw->ap_chan.tx_power;
#endif
    req->bcn_int = settings->beacon_interval;
    if (settings->crypto.control_port)
        flags |= CONTROL_PORT_HOST;

    if (settings->crypto.control_port_no_encrypt)
        flags |= CONTROL_PORT_NO_ENC;

    if (use_pairwise_key(&settings->crypto))
        flags |= WPA_WPA2_IN_USE;

    if (settings->crypto.control_port_ethertype)
        req->ctrl_port_ethertype = settings->crypto.control_port_ethertype;
    else
        req->ctrl_port_ethertype = ETH_P_PAE;
    req->flags = flags;

    /* Send the APM_START_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, APM_START_CFM, cfm);
}

int rwnx_send_apm_stop_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif)
{
    struct apm_stop_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the APM_STOP_REQ message */
    req = rwnx_msg_zalloc(APM_STOP_REQ, TASK_APM, DRV_TASK_ID,
                                   sizeof(struct apm_stop_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the APM_STOP_REQ message */
    req->vif_idx = vif->vif_index;

    /* Send the APM_STOP_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, APM_STOP_CFM, NULL);
}

uint8_t scanning = 0;
uint8_t p2p_working = 0;

#define P2P_WILDCARD_SSID                       "DIRECT-"
#define P2P_WILDCARD_SSID_LEN                   (sizeof(P2P_WILDCARD_SSID) - 1)


#ifdef CONFIG_SET_VENDOR_EXTENSION_IE
u8_l vendor_extension_data[256];
u8_l vendor_extension_len = 0;
#if 0
u8_l vendor_extension_data[]={
	0x10,0x49,0x00,0x17,0x00,0x01,0x37,0x10,
	0x06,0x00,0x10,0xc5,0xc9,0x91,0xeb,0x1f,
	0xce,0x4d,0x00,0xa1,0x2a,0xdf,0xa1,0xe9,
	0xc3,0x44,0xe6,0x10,0x49,0x00,0x21,0x00,
	0x01,0x37,0x20,0x01,0x00,0x01,0x05,0x20,
	0x02,0x00,0x04,0x43,0x56,0x54,0x45,0x20,
	0x05,0x00,0x0d,0x31,0x39,0x32,0x2e,0x31,
	0x36,0x38,0x2e,0x31,0x35,0x34,0x2e,0x31};
#endif

void rwnx_insert_vendor_extension_data(struct scanu_vendor_ie_req *ie_req){
	u8_l temp_ie[256];
	u8_l vendor_extension_subelement[3] = {0x00,0x37,0x2A};
	u8_l vendor_extension_id[2] = {0x10,0x49};
	int index = 0;
	int vendor_extension_subelement_len = 0;

	memset(temp_ie, 0, 256);

	//find vendor_extension_subelement
	for(index = 0; index < ie_req->add_ie_len; index++){
		if(ie_req->ie[index] == vendor_extension_id[0]){
			index++;
			if(index == ie_req->add_ie_len){
				return;
			}
			if(ie_req->ie[index] == vendor_extension_id[1] &&
				ie_req->ie[index + 3] == vendor_extension_subelement[0]&&
				ie_req->ie[index + 4] == vendor_extension_subelement[1]&&
				ie_req->ie[index + 5] == vendor_extension_subelement[2]){
				index = index + 2;
				vendor_extension_subelement_len = ie_req->ie[index];
				AICWFDBG(LOGDEBUG, "%s find vendor_extension_subelement,index:%d len:%d\r\n", __func__, index, ie_req->ie[index]);
				break;
			}
		}
	}
	index = index + vendor_extension_subelement_len;

	//insert vendor extension
	memcpy(&temp_ie[0], ie_req->ie, index + 1);
	memcpy(&temp_ie[index + 1], vendor_extension_data, vendor_extension_len/*sizeof(vendor_extension_data)*/);//insert vendor extension data
	memcpy(&temp_ie[index + 1 + vendor_extension_len/*sizeof(vendor_extension_data)*/], &ie_req->ie[index + 1], ie_req->add_ie_len - index);

	memcpy(ie_req->ie, temp_ie, ie_req->add_ie_len + vendor_extension_len/*sizeof(vendor_extension_data)*/);
	ie_req->add_ie_len = ie_req->add_ie_len + vendor_extension_len/*sizeof(vendor_extension_data)*/;
	ie_req->ie[1] = ie_req->ie[1] + vendor_extension_len/*sizeof(vendor_extension_data)*/;

	//rwnx_data_dump((char*)__func__, (void*)ie_req->ie, ie_req->add_ie_len);
}
#endif//CONFIG_SET_VENDOR_EXTENSION_IE

int rwnx_send_scanu_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
                        struct cfg80211_scan_request *param)
{
    struct scanu_start_req *req = NULL;
    struct scanu_vendor_ie_req *ie_req = NULL;
    struct mm_add_if_cfm add_if_cfm;
    int i;
    uint8_t chan_flags = 0;
    int err;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the SCANU_START_REQ message */
    req = rwnx_msg_zalloc(SCANU_START_REQ, TASK_SCANU, DRV_TASK_ID,
                          sizeof(struct scanu_start_req));
    if (!req)
        return -ENOMEM;

    scanning = 1;
    /* Set parameters */
    req->vif_idx = rwnx_vif->vif_index;
    req->chan_cnt = (u8)min_t(int, SCAN_CHANNEL_MAX, param->n_channels);
    req->ssid_cnt = (u8)min_t(int, SCAN_SSID_MAX, param->n_ssids);
    req->bssid = mac_addr_bcst;
    req->no_cck = param->no_cck;

#ifdef RADAR_OR_IR_DETECT
    if (req->ssid_cnt == 0)
        chan_flags |= CHAN_NO_IR;
#endif
    for (i = 0; i < req->ssid_cnt; i++) {
        int j;
        for (j = 0; j < param->ssids[i].ssid_len; j++)
            req->ssid[i].array[j] = param->ssids[i].ssid[j];
        req->ssid[i].length = param->ssids[i].ssid_len;

        if (!memcmp(P2P_WILDCARD_SSID, param->ssids[i].ssid,
            P2P_WILDCARD_SSID_LEN)) {
            AICWFDBG(LOGINFO, "p2p scanu:%d,%d,%d\n",rwnx_vif->vif_index, rwnx_vif->is_p2p_vif, rwnx_hw->is_p2p_alive);
#ifdef CONFIG_USE_P2P0
            if (rwnx_vif->is_p2p_vif && !rwnx_hw->is_p2p_alive) {
#else
            if (rwnx_vif == rwnx_hw->p2p_dev_vif && !rwnx_vif->up) {
#endif
				err = rwnx_send_add_if (rwnx_hw, rwnx_vif->wdev.address,
											  RWNX_VIF_TYPE(rwnx_vif), false, &add_if_cfm);
				if (err)
				   goto error;

				/*
                			if ((err = rwnx_send_add_if(rwnx_hw, rwnx_vif->wdev.address,
                                              RWNX_VIF_TYPE(rwnx_vif), false, &add_if_cfm)))
                  			goto error;
                   	*/

                if (add_if_cfm.status != 0) {
                    return -EIO;
                }

                /* Save the index retrieved from LMAC */
                spin_lock_bh(&rwnx_hw->cb_lock);
                rwnx_vif->vif_index = add_if_cfm.inst_nbr;
                rwnx_vif->up = true;
                rwnx_hw->vif_started++;
                rwnx_hw->vif_table[add_if_cfm.inst_nbr] = rwnx_vif;
                spin_unlock_bh(&rwnx_hw->cb_lock);
            }
            rwnx_hw->is_p2p_alive = 1;
#ifndef CONFIG_USE_P2P0
            mod_timer(&rwnx_hw->p2p_alive_timer, jiffies + msecs_to_jiffies(1000));
            atomic_set(&rwnx_hw->p2p_alive_timer_count, 0);
#endif
            AICWFDBG(LOGINFO ,"p2p scan start\n");
#ifdef CONFIG_STA_SCAN_WHEN_P2P_WORKING
			p2p_working = 0;
#else
			p2p_working = 1;
#endif
        }
    }

#if 1
    if (param->ie) {
        #if 0
        if (rwnx_ipc_elem_var_allocs(rwnx_hw, &rwnx_hw->scan_ie,
                                     param->ie_len, DMA_TO_DEVICE,
                                     NULL, param->ie, NULL))
            goto error;

        req->add_ie_len = param->ie_len;
        req->add_ies = rwnx_hw->scan_ie.dma_addr;
        #else
        ie_req = rwnx_msg_zalloc(SCANU_VENDOR_IE_REQ, TASK_SCANU, DRV_TASK_ID,
                              sizeof(struct scanu_vendor_ie_req));
        if (!ie_req)
            return -ENOMEM;

        ie_req->add_ie_len = param->ie_len;
        ie_req->vif_idx = rwnx_vif->vif_index;
        memcpy(ie_req->ie, param->ie, param->ie_len);
#ifdef CONFIG_SET_VENDOR_EXTENSION_IE
		rwnx_insert_vendor_extension_data(ie_req);
#endif //CONFIG_SET_VENDOR_EXTENSION_IE
        req->add_ie_len = 0;
        req->add_ies = 0;

        if ((err = rwnx_send_msg(rwnx_hw, ie_req, 1, SCANU_VENDOR_IE_CFM, NULL)))
            goto error;
        #endif
        }
    else {
        req->add_ie_len = 0;
        req->add_ies = 0;
    }
#else
        req->add_ie_len = 0;
        req->add_ies = 0;
#endif

    for (i = 0; i < req->chan_cnt; i++) {
        struct ieee80211_channel *chan = param->channels[i];
		AICWFDBG(LOGDEBUG, "scan channel:%d(%d) \r\n", ieee80211_frequency_to_channel(chan->center_freq), chan->center_freq);
        req->chan[i].band = chan->band;
        req->chan[i].freq = chan->center_freq;
        req->chan[i].flags = chan_flags | get_chan_flags(chan->flags);
        req->chan[i].tx_power = chan_to_fw_pwr(chan->max_reg_power);
    }

    /* Send the SCANU_START_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1,  SCANU_START_CFM_ADDTIONAL, NULL);
error:
    if (req != NULL)
        rwnx_msg_free(rwnx_hw, req);
    if (ie_req != NULL)
        rwnx_msg_free(rwnx_hw, ie_req);
    return -ENOMEM;
}

int rwnx_send_scanu_cancel_req(struct rwnx_hw *rwnx_hw, struct scan_cancel_cfm *cfm)
{
    struct scan_cancel_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the SCAN_CANCEL_REQ message */
    req = rwnx_msg_zalloc(SCANU_CANCEL_REQ, TASK_SCANU, DRV_TASK_ID,
                          sizeof(struct scan_cancel_req));
    if (!req)
        return -ENOMEM;

    /* Send the SCAN_CANCEL_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, SCANU_CANCEL_CFM, cfm);
}

int rwnx_send_apm_start_cac_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
                                struct cfg80211_chan_def *chandef,
                                struct apm_start_cac_cfm *cfm)
{
    struct apm_start_cac_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the APM_START_CAC_REQ message */
    req = rwnx_msg_zalloc(APM_START_CAC_REQ, TASK_APM, DRV_TASK_ID,
                          sizeof(struct apm_start_cac_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the APM_START_CAC_REQ message */
    req->vif_idx = vif->vif_index;
    req->chan.band = chandef->chan->band;
    req->chan.freq = chandef->chan->center_freq;
    req->chan.flags = 0;
    req->center_freq1 = chandef->center_freq1;
    req->center_freq2 = chandef->center_freq2;
    req->ch_width = bw2chnl[chandef->width];

    /* Send the APM_START_CAC_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, APM_START_CAC_CFM, cfm);
}

int rwnx_send_apm_stop_cac_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif)
{
    struct apm_stop_cac_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the APM_STOP_CAC_REQ message */
    req = rwnx_msg_zalloc(APM_STOP_CAC_REQ, TASK_APM, DRV_TASK_ID,
                          sizeof(struct apm_stop_cac_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the APM_STOP_CAC_REQ message */
    req->vif_idx = vif->vif_index;

    /* Send the APM_STOP_CAC_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, APM_STOP_CAC_CFM, NULL);
}

int rwnx_send_mesh_start_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
                             const struct mesh_config *conf, const struct mesh_setup *setup,
                             struct mesh_start_cfm *cfm)
{
    // Message to send
    struct mesh_start_req *req;
    // Supported basic rates
    struct ieee80211_supported_band *band_2GHz = rwnx_hw->wiphy->bands[NL80211_BAND_2GHZ];
    /* Counter */
    int i;
    /* Return status */
    int status;
    /* DMA Address to be unmapped after confirmation reception */
    u32 dma_addr = 0;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MESH_START_REQ message */
    req = rwnx_msg_zalloc(MESH_START_REQ, TASK_MESH, DRV_TASK_ID,
                          sizeof(struct mesh_start_req));
    if (!req) {
        return -ENOMEM;
    }

    req->vif_index = vif->vif_index;
 #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
    req->bcn_int = setup->beacon_interval;
    req->dtim_period = setup->dtim_period;
#endif
    req->mesh_id_len = setup->mesh_id_len;

    for (i = 0; i < setup->mesh_id_len; i++) {
        req->mesh_id[i] = *(setup->mesh_id + i);
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
    req->user_mpm = setup->user_mpm;
#endif
    req->is_auth = setup->is_authenticated;
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
    req->auth_id = setup->auth_id;
    #endif
    req->ie_len = setup->ie_len;

    if (setup->ie_len) {
        /*
         * Need to provide a Virtual Address to the MAC so that it can download the
         * additional information elements.
         */
        req->ie_addr = dma_map_single(rwnx_hw->dev, (void *)setup->ie,
                                      setup->ie_len, DMA_FROM_DEVICE);

        /* Check DMA mapping result */
        if (dma_mapping_error(rwnx_hw->dev, req->ie_addr)) {
            printk(KERN_CRIT "%s - DMA Mapping error on additional IEs\n", __func__);

            /* Consider there is no Additional IEs */
            req->ie_len = 0;
        } else {
            /* Store DMA Address so that we can unmap the memory section once MESH_START_CFM is received */
            dma_addr = req->ie_addr;
        }
    }

    /* Provide rate information */
    req->basic_rates.length = 0;
    for (i = 0; i < band_2GHz->n_bitrates; i++) {
        u16 rate = band_2GHz->bitrates[i].bitrate;

        /* Read value is in in units of 100 Kbps, provided value is in units
         * of 1Mbps, and multiplied by 2 so that 5.5 becomes 11 */
        rate = (rate << 1) / 10;

        #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0) // TODO: check basic rates
        if (setup->basic_rates & CO_BIT(i)) {
            rate |= 0x80;
        }
        #endif

        req->basic_rates.array[i] = (u8)rate;
        req->basic_rates.length++;
    }

    /* Provide channel information */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
    req->chan.band = setup->chandef.chan->band;
    req->chan.freq = setup->chandef.chan->center_freq;
#endif

    req->chan.flags = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
    req->chan.tx_power = chan_to_fw_pwr(setup->chandef.chan->max_power);
    req->center_freq1 = setup->chandef.center_freq1;
    req->center_freq2 = setup->chandef.center_freq2;
    req->ch_width = bw2chnl[setup->chandef.width];
#endif

    /* Send the MESH_START_REQ message to UMAC FW */
    status = rwnx_send_msg(rwnx_hw, req, 1, MESH_START_CFM, cfm);

    /* Unmap DMA area */
    if (setup->ie_len) {
        dma_unmap_single(rwnx_hw->dev, dma_addr, setup->ie_len, DMA_TO_DEVICE);
    }

    /* Return the status */
    return (status);
}

int rwnx_send_mesh_stop_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
                            struct mesh_stop_cfm *cfm)
{
    // Message to send
    struct mesh_stop_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MESH_STOP_REQ message */
    req = rwnx_msg_zalloc(MESH_STOP_REQ, TASK_MESH, DRV_TASK_ID,
                          sizeof(struct mesh_stop_req));
    if (!req) {
        return -ENOMEM;
    }

    req->vif_idx = vif->vif_index;

    /* Send the MESH_STOP_REQ message to UMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, MESH_STOP_CFM, cfm);
}

int rwnx_send_mesh_update_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
                              u32 mask, const struct mesh_config *p_mconf, struct mesh_update_cfm *cfm)
{
    // Message to send
    struct mesh_update_req *req;
    // Keep only bit for fields which can be updated
    u32 supp_mask = (mask << 1) & (CO_BIT(NL80211_MESHCONF_GATE_ANNOUNCEMENTS)
                                   | CO_BIT(NL80211_MESHCONF_HWMP_ROOTMODE)
                                   | CO_BIT(NL80211_MESHCONF_FORWARDING)
                                   | CO_BIT(NL80211_MESHCONF_POWER_MODE));


    RWNX_DBG(RWNX_FN_ENTRY_STR);

    if (!supp_mask) {
        return -ENOENT;
    }

    /* Build the MESH_UPDATE_REQ message */
    req = rwnx_msg_zalloc(MESH_UPDATE_REQ, TASK_MESH, DRV_TASK_ID,
                          sizeof(struct mesh_update_req));

    if (!req) {
        return -ENOMEM;
    }

    req->vif_idx = vif->vif_index;

    if (supp_mask & CO_BIT(NL80211_MESHCONF_GATE_ANNOUNCEMENTS))
    {
        req->flags |= CO_BIT(MESH_UPDATE_FLAGS_GATE_MODE_BIT);
        req->gate_announ = p_mconf->dot11MeshGateAnnouncementProtocol;
    }

    if (supp_mask & CO_BIT(NL80211_MESHCONF_HWMP_ROOTMODE))
    {
        req->flags |= CO_BIT(MESH_UPDATE_FLAGS_ROOT_MODE_BIT);
        req->root_mode = p_mconf->dot11MeshHWMPRootMode;
    }

    if (supp_mask & CO_BIT(NL80211_MESHCONF_FORWARDING))
    {
        req->flags |= CO_BIT(MESH_UPDATE_FLAGS_MESH_FWD_BIT);
        req->mesh_forward = p_mconf->dot11MeshForwarding;
    }

    if (supp_mask & CO_BIT(NL80211_MESHCONF_POWER_MODE))
    {
        req->flags |= CO_BIT(MESH_UPDATE_FLAGS_LOCAL_PSM_BIT);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
        req->local_ps_mode = p_mconf->power_mode;
#endif
    }

    /* Send the MESH_UPDATE_REQ message to UMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, MESH_UPDATE_CFM, cfm);
}

int rwnx_send_mesh_peer_info_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
                                 u8 sta_idx, struct mesh_peer_info_cfm *cfm)
{
    // Message to send
    struct mesh_peer_info_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MESH_PEER_INFO_REQ message */
    req = rwnx_msg_zalloc(MESH_PEER_INFO_REQ, TASK_MESH, DRV_TASK_ID,
                          sizeof(struct mesh_peer_info_req));
    if (!req) {
        return -ENOMEM;
    }

    req->sta_idx = sta_idx;

    /* Send the MESH_PEER_INFO_REQ message to UMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, MESH_PEER_INFO_CFM, cfm);
}

void rwnx_send_mesh_peer_update_ntf(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
                                    u8 sta_idx, u8 mlink_state)
{
    // Message to send
    struct mesh_peer_update_ntf *ntf;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MESH_PEER_UPDATE_NTF message */
    ntf = rwnx_msg_zalloc(MESH_PEER_UPDATE_NTF, TASK_MESH, DRV_TASK_ID,
                          sizeof(struct mesh_peer_update_ntf));

    if (ntf) {
        ntf->vif_idx = vif->vif_index;
        ntf->sta_idx = sta_idx;
        ntf->state = mlink_state;

        /* Send the MESH_PEER_INFO_REQ message to UMAC FW */
        rwnx_send_msg(rwnx_hw, ntf, 0, 0, NULL);
    }
}

void rwnx_send_mesh_path_create_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, u8 *tgt_addr)
{
    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Check if we are already waiting for a confirmation */
    if (!vif->ap.create_path) {
        // Message to send
        struct mesh_path_create_req *req;

        /* Build the MESH_PATH_CREATE_REQ message */
        req = rwnx_msg_zalloc(MESH_PATH_CREATE_REQ, TASK_MESH, DRV_TASK_ID,
                              sizeof(struct mesh_path_create_req));

        if (req) {
            req->vif_idx = vif->vif_index;
            memcpy(&req->tgt_mac_addr, tgt_addr, ETH_ALEN);

            vif->ap.create_path = true;

            /* Send the MESH_PATH_CREATE_REQ message to UMAC FW */
            rwnx_send_msg(rwnx_hw, req, 0, 0, NULL);
        }
    }
}

int rwnx_send_mesh_path_update_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, const u8 *tgt_addr,
                                   const u8 *p_nhop_addr, struct mesh_path_update_cfm *cfm)
{
    // Message to send
    struct mesh_path_update_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MESH_PATH_UPDATE_REQ message */
    req = rwnx_msg_zalloc(MESH_PATH_UPDATE_REQ, TASK_MESH, DRV_TASK_ID,
                          sizeof(struct mesh_path_update_req));
    if (!req) {
        return -ENOMEM;
    }

    req->delete = (p_nhop_addr == NULL);
    req->vif_idx = vif->vif_index;
    memcpy(&req->tgt_mac_addr, tgt_addr, ETH_ALEN);

    if (p_nhop_addr) {
        memcpy(&req->nhop_mac_addr, p_nhop_addr, ETH_ALEN);
    }

    /* Send the MESH_PATH_UPDATE_REQ message to UMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, MESH_PATH_UPDATE_CFM, cfm);
}

void rwnx_send_mesh_proxy_add_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, u8 *ext_addr)
{
    // Message to send
    struct mesh_proxy_add_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MESH_PROXY_ADD_REQ message */
    req = rwnx_msg_zalloc(MESH_PROXY_ADD_REQ, TASK_MESH, DRV_TASK_ID,
                          sizeof(struct mesh_proxy_add_req));

    if (req) {
        req->vif_idx = vif->vif_index;
        memcpy(&req->ext_sta_addr, ext_addr, ETH_ALEN);

        /* Send the MESH_PROXY_ADD_REQ message to UMAC FW */
        rwnx_send_msg(rwnx_hw, req, 0, 0, NULL);
    }
}

int rwnx_send_tdls_peer_traffic_ind_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif)
{
    struct tdls_peer_traffic_ind_req *tdls_peer_traffic_ind_req;

    if (!rwnx_vif->sta.tdls_sta)
        return -ENOLINK;

    /* Build the TDLS_PEER_TRAFFIC_IND_REQ message */
    tdls_peer_traffic_ind_req = rwnx_msg_zalloc(TDLS_PEER_TRAFFIC_IND_REQ, TASK_TDLS, DRV_TASK_ID,
                                           sizeof(struct tdls_peer_traffic_ind_req));

    if (!tdls_peer_traffic_ind_req)
        return -ENOMEM;

    /* Set parameters for the TDLS_PEER_TRAFFIC_IND_REQ message */
    tdls_peer_traffic_ind_req->vif_index = rwnx_vif->vif_index;
    tdls_peer_traffic_ind_req->sta_idx = rwnx_vif->sta.tdls_sta->sta_idx;
    memcpy(&(tdls_peer_traffic_ind_req->peer_mac_addr.array[0]),
           rwnx_vif->sta.tdls_sta->mac_addr, ETH_ALEN);
    tdls_peer_traffic_ind_req->dialog_token = 0; // check dialog token value
    tdls_peer_traffic_ind_req->last_tid = rwnx_vif->sta.tdls_sta->tdls.last_tid;
    tdls_peer_traffic_ind_req->last_sn = rwnx_vif->sta.tdls_sta->tdls.last_sn;

    /* Send the TDLS_PEER_TRAFFIC_IND_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, tdls_peer_traffic_ind_req, 0, 0, NULL);
}

int rwnx_send_config_monitor_req(struct rwnx_hw *rwnx_hw,
                                 struct cfg80211_chan_def *chandef,
                                 struct me_config_monitor_cfm *cfm)
{
    struct me_config_monitor_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the ME_CONFIG_MONITOR_REQ message */
    req = rwnx_msg_zalloc(ME_CONFIG_MONITOR_REQ, TASK_ME, DRV_TASK_ID,
                                   sizeof(struct me_config_monitor_req));
    if (!req)
        return -ENOMEM;

    if (chandef) {
        req->chan_set = true;

        req->chan.band = chandef->chan->band;
        req->chan.type = bw2chnl[chandef->width];
        req->chan.prim20_freq = chandef->chan->center_freq;
        req->chan.center1_freq = chandef->center_freq1;
        req->chan.center2_freq = chandef->center_freq2;
        req->chan.tx_power = chan_to_fw_pwr(chandef->chan->max_power);

        if (rwnx_hw->phy.limit_bw)
            limit_chan_bw(&req->chan.type, req->chan.prim20_freq, &req->chan.center1_freq);
    } else {
         req->chan_set = false;
    }

    req->uf = rwnx_hw->mod_params->uf;
    req->auto_reply = rwnx_hw->mod_params->auto_reply;

    /* Send the ME_CONFIG_MONITOR_REQ message to FW */
    return rwnx_send_msg(rwnx_hw, req, 1, ME_CONFIG_MONITOR_CFM, cfm);
}
#endif /* CONFIG_RWNX_FULLMAC */

int rwnx_send_tdls_chan_switch_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
                                   struct rwnx_sta *rwnx_sta, bool sta_initiator,
                                   u8 oper_class, struct cfg80211_chan_def *chandef,
                                   struct tdls_chan_switch_cfm *cfm)
{
    struct tdls_chan_switch_req *tdls_chan_switch_req;


    /* Build the TDLS_CHAN_SWITCH_REQ message */
    tdls_chan_switch_req = rwnx_msg_zalloc(TDLS_CHAN_SWITCH_REQ, TASK_TDLS, DRV_TASK_ID,
                                           sizeof(struct tdls_chan_switch_req));

    if (!tdls_chan_switch_req)
        return -ENOMEM;

    /* Set parameters for the TDLS_CHAN_SWITCH_REQ message */
    tdls_chan_switch_req->vif_index = rwnx_vif->vif_index;
    tdls_chan_switch_req->sta_idx = rwnx_sta->sta_idx;
    memcpy(&(tdls_chan_switch_req->peer_mac_addr.array[0]),
           rwnx_sta_addr(rwnx_sta), ETH_ALEN);
    tdls_chan_switch_req->initiator = sta_initiator;
    tdls_chan_switch_req->band = chandef->chan->band;
    tdls_chan_switch_req->type = bw2chnl[chandef->width];
    tdls_chan_switch_req->prim20_freq = chandef->chan->center_freq;
    tdls_chan_switch_req->center1_freq = chandef->center_freq1;
    tdls_chan_switch_req->center2_freq = chandef->center_freq2;
    tdls_chan_switch_req->tx_power = chan_to_fw_pwr(chandef->chan->max_power);
    tdls_chan_switch_req->op_class = oper_class;

    /* Send the TDLS_CHAN_SWITCH_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, tdls_chan_switch_req, 1, TDLS_CHAN_SWITCH_CFM, cfm);
}

int rwnx_send_tdls_cancel_chan_switch_req(struct rwnx_hw *rwnx_hw,
                                          struct rwnx_vif *rwnx_vif,
                                          struct rwnx_sta *rwnx_sta,
                                          struct tdls_cancel_chan_switch_cfm *cfm)
{
    struct tdls_cancel_chan_switch_req *tdls_cancel_chan_switch_req;

    /* Build the TDLS_CHAN_SWITCH_REQ message */
    tdls_cancel_chan_switch_req = rwnx_msg_zalloc(TDLS_CANCEL_CHAN_SWITCH_REQ, TASK_TDLS, DRV_TASK_ID,
                                           sizeof(struct tdls_cancel_chan_switch_req));
    if (!tdls_cancel_chan_switch_req)
        return -ENOMEM;

    /* Set parameters for the TDLS_CHAN_SWITCH_REQ message */
    tdls_cancel_chan_switch_req->vif_index = rwnx_vif->vif_index;
    tdls_cancel_chan_switch_req->sta_idx = rwnx_sta->sta_idx;
    memcpy(&(tdls_cancel_chan_switch_req->peer_mac_addr.array[0]),
           rwnx_sta_addr(rwnx_sta), ETH_ALEN);

    /* Send the TDLS_CHAN_SWITCH_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, tdls_cancel_chan_switch_req, 1, TDLS_CANCEL_CHAN_SWITCH_CFM, cfm);
}

#ifdef CONFIG_RWNX_BFMER
#ifdef CONFIG_RWNX_FULLMAC
void rwnx_send_bfmer_enable(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta,
                            const struct ieee80211_vht_cap *vht_cap)
#endif /* CONFIG_RWNX_FULLMAC*/
{
    struct mm_bfmer_enable_req *bfmer_en_req;
#ifdef CONFIG_RWNX_FULLMAC
    __le32 vht_capability;
    u8 rx_nss = 0;
#endif /* CONFIG_RWNX_FULLMAC */

    RWNX_DBG(RWNX_FN_ENTRY_STR);

#ifdef CONFIG_RWNX_FULLMAC
    if (!vht_cap) {
#endif /* CONFIG_RWNX_FULLMAC */
        goto end;
    }

#ifdef CONFIG_RWNX_FULLMAC
    vht_capability = vht_cap->vht_cap_info;
#endif /* CONFIG_RWNX_FULLMAC */

    if (!(vht_capability & IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE)) {
        goto end;
    }

#ifdef CONFIG_RWNX_FULLMAC
    rx_nss = rwnx_bfmer_get_rx_nss(vht_cap);
#endif /* CONFIG_RWNX_FULLMAC */

    /* Allocate a structure that will contain the beamforming report */
    if (rwnx_bfmer_report_add(rwnx_hw, rwnx_sta, RWNX_BFMER_REPORT_SPACE_SIZE))
    {
        goto end;
    }

    /* Build the MM_BFMER_ENABLE_REQ message */
    bfmer_en_req = rwnx_msg_zalloc(MM_BFMER_ENABLE_REQ, TASK_MM, DRV_TASK_ID,
                                   sizeof(struct mm_bfmer_enable_req));

    /* Check message allocation */
    if (!bfmer_en_req) {
        /* Free memory allocated for the report */
        rwnx_bfmer_report_del(rwnx_hw, rwnx_sta);

        /* Do not use beamforming */
        goto end;
    }

    /* Provide DMA address to the MAC */
    bfmer_en_req->host_bfr_addr = rwnx_sta->bfm_report->dma_addr;
    bfmer_en_req->host_bfr_size = RWNX_BFMER_REPORT_SPACE_SIZE;
    bfmer_en_req->sta_idx = rwnx_sta->sta_idx;
#ifdef CONFIG_RWNX_FULLMAC
    bfmer_en_req->aid = rwnx_sta->aid;
    bfmer_en_req->rx_nss = rx_nss;
#endif /* CONFIG_RWNX_FULLMAC */

    if (vht_capability & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE) {
        bfmer_en_req->vht_mu_bfmee = true;
    } else {
        bfmer_en_req->vht_mu_bfmee = false;
    }

    /* Send the MM_BFMER_EN_REQ message to LMAC FW */
    rwnx_send_msg(rwnx_hw, bfmer_en_req, 0, 0, NULL);

end:
    return;
}

#ifdef CONFIG_RWNX_MUMIMO_TX
int rwnx_send_mu_group_update_req(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta)
{
    struct mm_mu_group_update_req *req;
    int group_id, i = 0;
    u64 map;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_MU_GROUP_UPDATE_REQ message */
    req = rwnx_msg_zalloc(MM_MU_GROUP_UPDATE_REQ, TASK_MM, DRV_TASK_ID,
                          sizeof(struct mm_mu_group_update_req) +
                          rwnx_sta->group_info.cnt * sizeof(req->groups[0]));

    /* Check message allocation */
    if (!req)
        return -ENOMEM;

    /* Go through the groups the STA belongs to */
    group_sta_for_each(rwnx_sta, group_id, map) {
        int user_pos = rwnx_mu_group_sta_get_pos(rwnx_hw, rwnx_sta, group_id);

        if (WARN((i >= rwnx_sta->group_info.cnt),
                 "STA%d: Too much group (%d)\n",
                 rwnx_sta->sta_idx, i + 1))
            break;

        req->groups[i].group_id = group_id;
        req->groups[i].user_pos = user_pos;

        i++;
    }

    req->group_cnt = rwnx_sta->group_info.cnt;
    req->sta_idx = rwnx_sta->sta_idx;

    /* Send the MM_MU_GROUP_UPDATE_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, MM_MU_GROUP_UPDATE_CFM, NULL);
}
#endif /* CONFIG_RWNX_MUMIMO_TX */
#endif /* CONFIG_RWNX_BFMER */

/**********************************************************************
 *    Debug Messages
 *********************************************************************/
int rwnx_send_dbg_trigger_req(struct rwnx_hw *rwnx_hw, char *msg)
{
    struct mm_dbg_trigger_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_DBG_TRIGGER_REQ message */
    req = rwnx_msg_zalloc(MM_DBG_TRIGGER_REQ, TASK_MM, DRV_TASK_ID,
                          sizeof(struct mm_dbg_trigger_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the MM_DBG_TRIGGER_REQ message */
    strncpy(req->error, msg, sizeof(req->error));

    /* Send the MM_DBG_TRIGGER_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 0, -1, NULL);
}

int rwnx_send_dbg_mem_read_req(struct rwnx_hw *rwnx_hw, u32 mem_addr,
                               struct dbg_mem_read_cfm *cfm)
{
    struct dbg_mem_read_req *mem_read_req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the DBG_MEM_READ_REQ message */
    mem_read_req = rwnx_msg_zalloc(DBG_MEM_READ_REQ, TASK_DBG, DRV_TASK_ID,
                                   sizeof(struct dbg_mem_read_req));
    if (!mem_read_req)
        return -ENOMEM;

    /* Set parameters for the DBG_MEM_READ_REQ message */
    mem_read_req->memaddr = mem_addr;

    /* Send the DBG_MEM_READ_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, mem_read_req, 1, DBG_MEM_READ_CFM, cfm);
}

int rwnx_send_dbg_mem_write_req(struct rwnx_hw *rwnx_hw, u32 mem_addr,
                                u32 mem_data)
{
    struct dbg_mem_write_req *mem_write_req;

    //RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the DBG_MEM_WRITE_REQ message */
    mem_write_req = rwnx_msg_zalloc(DBG_MEM_WRITE_REQ, TASK_DBG, DRV_TASK_ID,
                                    sizeof(struct dbg_mem_write_req));
    if (!mem_write_req)
        return -ENOMEM;

    /* Set parameters for the DBG_MEM_WRITE_REQ message */
    mem_write_req->memaddr = mem_addr;
    mem_write_req->memdata = mem_data;

    /* Send the DBG_MEM_WRITE_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, mem_write_req, 1, DBG_MEM_WRITE_CFM, NULL);
}

int rwnx_send_dbg_mem_mask_write_req(struct rwnx_hw *rwnx_hw, u32 mem_addr,
                                     u32 mem_mask, u32 mem_data)
{
    struct dbg_mem_mask_write_req *mem_mask_write_req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

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
    return rwnx_send_msg(rwnx_hw, mem_mask_write_req, 1, DBG_MEM_MASK_WRITE_CFM, NULL);
}

#ifdef CONFIG_RFTEST
int rwnx_send_rftest_req(struct rwnx_hw *rwnx_hw, u32_l cmd, u32_l argc, u8_l *argv, struct dbg_rftest_cmd_cfm *cfm)
{
    struct dbg_rftest_cmd_req *mem_rftest_cmd_req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the DBG_RFTEST_CMD_REQ message */
    mem_rftest_cmd_req = rwnx_msg_zalloc(DBG_RFTEST_CMD_REQ, TASK_DBG, DRV_TASK_ID,
                                         sizeof(struct dbg_rftest_cmd_req));
    if (!mem_rftest_cmd_req)
        return -ENOMEM;

    if(argc > 30)
        return -ENOMEM;

    /* Set parameters for the DBG_MEM_MASK_WRITE_REQ message */
    mem_rftest_cmd_req->cmd = cmd;
    mem_rftest_cmd_req->argc = argc;
    if(argc != 0)
        memcpy(mem_rftest_cmd_req->argv, argv, argc);

    /* Send the DBG_RFTEST_CMD_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, mem_rftest_cmd_req, 1, DBG_RFTEST_CMD_CFM, cfm);
}
#endif

int rwnx_send_dbg_set_mod_filter_req(struct rwnx_hw *rwnx_hw, u32 filter)
{
    struct dbg_set_mod_filter_req *set_mod_filter_req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the DBG_SET_MOD_FILTER_REQ message */
    set_mod_filter_req =
        rwnx_msg_zalloc(DBG_SET_MOD_FILTER_REQ, TASK_DBG, DRV_TASK_ID,
                        sizeof(struct dbg_set_mod_filter_req));
    if (!set_mod_filter_req)
        return -ENOMEM;

    /* Set parameters for the DBG_SET_MOD_FILTER_REQ message */
    set_mod_filter_req->mod_filter = filter;

    /* Send the DBG_SET_MOD_FILTER_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, set_mod_filter_req, 1, DBG_SET_MOD_FILTER_CFM, NULL);
}

int rwnx_send_dbg_set_sev_filter_req(struct rwnx_hw *rwnx_hw, u32 filter)
{
    struct dbg_set_sev_filter_req *set_sev_filter_req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the DBG_SET_SEV_FILTER_REQ message */
    set_sev_filter_req =
        rwnx_msg_zalloc(DBG_SET_SEV_FILTER_REQ, TASK_DBG, DRV_TASK_ID,
                        sizeof(struct dbg_set_sev_filter_req));
    if (!set_sev_filter_req)
        return -ENOMEM;

    /* Set parameters for the DBG_SET_SEV_FILTER_REQ message */
    set_sev_filter_req->sev_filter = filter;

    /* Send the DBG_SET_SEV_FILTER_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, set_sev_filter_req, 1, DBG_SET_SEV_FILTER_CFM, NULL);
}

int rwnx_send_dbg_get_sys_stat_req(struct rwnx_hw *rwnx_hw,
                                   struct dbg_get_sys_stat_cfm *cfm)
{
    void *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Allocate the message */
    req = rwnx_msg_zalloc(DBG_GET_SYS_STAT_REQ, TASK_DBG, DRV_TASK_ID, 0);
    if (!req)
        return -ENOMEM;

    /* Send the DBG_MEM_READ_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 1, DBG_GET_SYS_STAT_CFM, cfm);
}

int rwnx_send_dbg_mem_block_write_req(struct rwnx_hw *rwnx_hw, u32 mem_addr,
                                      u32 mem_size, u32 *mem_data)
{
    struct dbg_mem_block_write_req *mem_blk_write_req;

    //RWNX_DBG(RWNX_FN_ENTRY_STR);

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
    return rwnx_send_msg(rwnx_hw, mem_blk_write_req, 1, DBG_MEM_BLOCK_WRITE_CFM, NULL);
}

int rwnx_send_dbg_start_app_req(struct rwnx_hw *rwnx_hw, u32 boot_addr,
                                u32 boot_type)
{
    struct dbg_start_app_req *start_app_req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the DBG_START_APP_REQ message */
    start_app_req = rwnx_msg_zalloc(DBG_START_APP_REQ, TASK_DBG, DRV_TASK_ID,
                                    sizeof(struct dbg_start_app_req));
    if (!start_app_req)
        return -ENOMEM;

    /* Set parameters for the DBG_START_APP_REQ message */
    start_app_req->bootaddr = boot_addr;
    start_app_req->boottype = boot_type;

    /* Send the DBG_START_APP_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, start_app_req, 1, DBG_START_APP_CFM, NULL);
}

int rwnx_send_dbg_gpio_write_req(struct rwnx_hw *rwnx_hw, u8 gpio_idx, u8 gpio_val)
{
    struct dbg_gpio_write_req *gpio_write_req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    gpio_write_req = rwnx_msg_zalloc(DBG_GPIO_WRITE_REQ, TASK_DBG, DRV_TASK_ID,
                                    sizeof(struct dbg_gpio_write_req));
    if (!gpio_write_req)
        return -ENOMEM;

    gpio_write_req->gpio_idx  = gpio_idx;
    gpio_write_req->gpio_val  = gpio_val;

    return rwnx_send_msg(rwnx_hw, gpio_write_req, 1, DBG_GPIO_WRITE_CFM, NULL);
}

int rwnx_send_dbg_gpio_read_req(struct rwnx_hw *rwnx_hw, u8_l gpio_idx, struct dbg_gpio_read_cfm *cfm)
{
    struct dbg_gpio_read_req *gpio_read_req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    gpio_read_req = rwnx_msg_zalloc(DBG_GPIO_READ_REQ, TASK_DBG, DRV_TASK_ID,
                                    sizeof(struct dbg_gpio_read_req));
    if (!gpio_read_req)
        return -ENOMEM;

    gpio_read_req->gpio_idx  = gpio_idx;

    return rwnx_send_msg(rwnx_hw, gpio_read_req, 1, DBG_GPIO_READ_CFM, cfm);
}

int rwnx_send_dbg_gpio_init_req(struct rwnx_hw *rwnx_hw, u8_l gpio_idx, u8_l gpio_dir, u8_l gpio_val)
{
    struct dbg_gpio_init_req *gpio_init_req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    gpio_init_req = rwnx_msg_zalloc(DBG_GPIO_INIT_REQ, TASK_DBG, DRV_TASK_ID,
                                    sizeof(struct dbg_gpio_init_req));
    if (!gpio_init_req)
        return -ENOMEM;

    gpio_init_req->gpio_idx  = gpio_idx;
    gpio_init_req->gpio_dir  = gpio_dir;
    gpio_init_req->gpio_val  = gpio_val;

    return rwnx_send_msg(rwnx_hw, gpio_init_req, 1, DBG_GPIO_INIT_CFM, NULL);
}

int rwnx_send_cfg_rssi_req(struct rwnx_hw *rwnx_hw, u8 vif_index, int rssi_thold, u32 rssi_hyst)
{
    struct mm_cfg_rssi_req *req;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* Build the MM_CFG_RSSI_REQ message */
    req = rwnx_msg_zalloc(MM_CFG_RSSI_REQ, TASK_MM, DRV_TASK_ID,
                          sizeof(struct mm_cfg_rssi_req));
    if (!req)
        return -ENOMEM;

    if(rwnx_hw->vif_table[vif_index]==NULL)
	return 0;

    /* Set parameters for the MM_CFG_RSSI_REQ message */
    req->vif_index = vif_index;
    req->rssi_thold = (s8)rssi_thold;
    req->rssi_hyst = (u8)rssi_hyst;

    /* Send the MM_CFG_RSSI_REQ message to LMAC FW */
    return rwnx_send_msg(rwnx_hw, req, 0, 0, NULL);
}

#ifdef CONFIG_USB_BT
int rwnx_send_reboot(struct rwnx_hw *rwnx_hw)
{
    int ret = 0;
    u32 delay = 2 *1000; //1s

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    ret = rwnx_send_dbg_start_app_req(rwnx_hw, delay, HOST_START_APP_REBOOT);
    return ret;
}
#endif // CONFIG_USB_BT
