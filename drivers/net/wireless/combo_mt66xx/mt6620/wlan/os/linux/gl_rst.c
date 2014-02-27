/*
** $Id: @(#) gl_rst.c@@
*/

/*! \file   gl_rst.c
    \brief  Main routines for supporintg MT6620 whole-chip reset mechanism

    This file contains the support routines of Linux driver for MediaTek Inc. 802.11
    Wireless LAN Adapters.
*/



/*
** $Log: gl_rst.c $
 *
 * 11 10 2011 cp.wu
 * [WCXRP00001098] [MT6620 Wi-Fi][Driver] Replace printk by DBG LOG macros in linux porting layer
 * 1. eliminaite direct calls to printk in porting layer.
 * 2. replaced by DBGLOG, which would be XLOG on ALPS platforms.
 *
 * 04 22 2011 cp.wu
 * [WCXRP00000598] [MT6620 Wi-Fi][Driver] Implementation of interface for communicating with user space process for RESET_START and RESET_END events
 * skip power-off handshaking when RESET indication is received.
 *
 * 04 14 2011 cp.wu
 * [WCXRP00000598] [MT6620 Wi-Fi][Driver] Implementation of interface for communicating with user space process for RESET_START and RESET_END events
 * 1. add code to put whole-chip resetting trigger when abnormal firmware assertion is detected
 * 2. add dummy function for both Win32 and Linux part.
 *
 * 03 30 2011 cp.wu
 * [WCXRP00000598] [MT6620 Wi-Fi][Driver] Implementation of interface for communicating with user space process for RESET_START and RESET_END events
 * use netlink unicast instead of broadcast
 *
**
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "gl_os.h"
#include "debug.h"
#include "wlan_lib.h"
#include "gl_wext.h"
#include "precomp.h"
#include <linux/poll.h>
#include <net/netlink.h>
#include <net/genetlink.h>



#if CFG_CHIP_RESET_SUPPORT

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#define MAX_BIND_PROCESS    (4)

#define MTK_WIFI_FAMILY_NAME        "MTK_WIFI"
#define MTK_WIFI_RESET_START_NAME   "RESET_START"
#define MTK_WIFI_RESET_END_NAME     "RESET_END"
#define MTK_WIFI_RESET_TEST_NAME    "GENETLINK_START"


/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
enum {
    __MTK_WIFI_ATTR_INVALID,
    MTK_WIFI_ATTR_MSG,
    __MTK_WIFI_ATTR_MAX,
};
#define MTK_WIFI_ATTR_MAX       (__MTK_WIFI_ATTR_MAX - 1)


enum {
    __MTK_WIFI_COMMAND_INVALID,
    MTK_WIFI_COMMAND_BIND,
    MTK_WIFI_COMMAND_RESET,
    __MTK_WIFI_COMMAND_MAX,
};
#define MTK_WIFI_COMMAND_MAX    (__MTK_WIFI_COMMAND_MAX - 1)

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/BOOLEAN fgIsResetting = FALSE;
extern volatile int wlan_mode;
extern volatile set_p2p_mode pf_set_p2p_mode;
extern volatile int power_state;
extern MTK_WCN_BOOL
mtk_wcn_wmt_func_on (
    ENUM_WMTDRV_TYPE_T type
    );


/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/
static UINT_32 mtk_wifi_seqnum = 0;
static int num_bind_process = 0;
static pid_t bind_pid[MAX_BIND_PROCESS];
static struct delayed_work work_rst;
static set_p2p_mode pf_set_p2p_mode_rst;


/* attribute policy */
static struct nla_policy mtk_wifi_genl_policy[MTK_WIFI_ATTR_MAX + 1] = {
    [MTK_WIFI_ATTR_MSG] = { .type = NLA_NUL_STRING },
};

/* family definition */
static struct genl_family mtk_wifi_gnl_family = {
    .id         = GENL_ID_GENERATE,
    .hdrsize    = 0,
    .name       = MTK_WIFI_FAMILY_NAME,
    .version    = 1,
    .maxattr    = MTK_WIFI_ATTR_MAX,
};

/* forward declaration */
static int mtk_wifi_bind(
    struct sk_buff *skb,
    struct genl_info *info
    );

static int mtk_wifi_reset(
    void
    );

/* operation definition */
static struct genl_ops mtk_wifi_gnl_ops_bind = {
    .cmd = MTK_WIFI_COMMAND_BIND,
    .flags  = 0,
    .policy = mtk_wifi_genl_policy,
    .doit   = mtk_wifi_bind,
    .dumpit = NULL,
};

static struct genl_ops mtk_wifi_gnl_ops_reset = {
    .cmd = MTK_WIFI_COMMAND_RESET,
    .flags  = 0,
    .policy = mtk_wifi_genl_policy,
    .doit   = mtk_wifi_reset,
    .dumpit = NULL,
};


/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
extern int
mtk_wcn_wmt_msgcb_reg(
    ENUM_WMTDRV_TYPE_T eType,
    PF_WMT_CB pCb);

extern int
mtk_wcn_wmt_msgcb_unreg(
    ENUM_WMTDRV_TYPE_T eType
    );

static void *
glResetCallback (
    ENUM_WMTDRV_TYPE_T  eSrcType,
    ENUM_WMTDRV_TYPE_T  eDstType,
    ENUM_WMTMSG_TYPE_T  eMsgType,
    void *              prMsgBody,
    unsigned int        u4MsgLength
    );

static BOOLEAN
glResetSendMessage (
    char    *aucMsg,
    u8      cmd
    );


/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for 
 *        1. registering for reset callbacks
 *        2. initialize netlink socket 
 *
 * @param none
 *
 * @retval none
 */
/*----------------------------------------------------------------------------*/
VOID
glResetInit(
    VOID
    )
{
    /* 1. register for reset callback */
    mtk_wcn_wmt_msgcb_reg(WMTDRV_TYPE_WIFI, (PF_WMT_CB)glResetCallback);

    /* 2.1 registration for NETLINK_GENERIC family */
    if(genl_register_family(&mtk_wifi_gnl_family) != 0) {
        DBGLOG(INIT, WARN, ("%s(): GE_NELINK family registration fail\n", __func__));
    }
    else {
        /* 2.2 operation registration */
        if(genl_register_ops(&mtk_wifi_gnl_family, &mtk_wifi_gnl_ops_bind) != 0) {
            DBGLOG(INIT, WARN, ("%s(): BIND operation registration fail\n", __func__));
        }

        if(genl_register_ops(&mtk_wifi_gnl_family, &mtk_wifi_gnl_ops_reset) != 0) {
            DBGLOG(INIT, WARN, ("%s(): RESET operation registration fail\n", __func__));
        }
    }
	INIT_DELAYED_WORK(&work_rst, mtk_wifi_reset);

    return;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for 
 *        1. uninitialize netlink socket 
 *        2. deregistering for reset callbacks
 *
 * @param none
 *
 * @retval none
 */
/*----------------------------------------------------------------------------*/
VOID
glResetUninit(
    VOID
    )
{
    /* 1. release NETLINK_GENERIC family */
    genl_unregister_family(&mtk_wifi_gnl_family);

    /* 2. deregister for reset callback */
    mtk_wcn_wmt_msgcb_unreg(WMTDRV_TYPE_WIFI);

    return;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is invoked when there is reset messages indicated
 *
 * @param   eSrcType
 *          eDstType
 *          eMsgType
 *          prMsgBody
 *          u4MsgLength
 *
 * @retval 
 */
/*----------------------------------------------------------------------------*/
static void *
glResetCallback (
    ENUM_WMTDRV_TYPE_T  eSrcType,
    ENUM_WMTDRV_TYPE_T  eDstType,
    ENUM_WMTMSG_TYPE_T  eMsgType,
    void *              prMsgBody,
    unsigned int        u4MsgLength
    )
{
    PARAM_CUSTOM_P2P_SET_STRUC_T p2pmode;
    struct net_device *netdev = NULL;
	switch(eMsgType) {
    case WMTMSG_TYPE_RESET:
        if(u4MsgLength == sizeof(ENUM_WMTRSTMSG_TYPE_T)) {
            P_ENUM_WMTRSTMSG_TYPE_T prRstMsg = (P_ENUM_WMTRSTMSG_TYPE_T) prMsgBody;

            switch(*prRstMsg) {
            case WMTRSTMSG_RESET_START:
                fgIsResetting = TRUE;
				printk("WLAN driver:whole chip reset start!\n");
				//pf_set_p2p_mode_rst=pf_set_p2p_mode;
				if (pf_set_p2p_mode){
					netdev = dev_get_by_name(&init_net,"wlan0");
					p2pmode.u4Enable = 0;
			        p2pmode.u4Mode = 0;
				    pf_set_p2p_mode(netdev, p2pmode);
					msleep(300);
					dev_put(netdev);
			        netdev = NULL;
					}
                break;

            case WMTRSTMSG_RESET_END:
                printk("WLAN driver:whole chip reset end\n");
                fgIsResetting = FALSE;
				if (power_state==0)
					{
					 break;
					}
				else
					{
					 schedule_delayed_work(&work_rst, 200);
					 break;
					 }
			 default:
                break;
					}
            }
		default:
        break;
        }
	return NULL;
    }


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine send out message via netlink socket
 *
 * @param   aucMsg
 *          u4MsgLength
 *
 * @retval  TRUE
 *          FALSE
 */
/*----------------------------------------------------------------------------*/
static BOOLEAN
glResetSendMessage(
    char *  aucMsg,
    u8      cmd
    )
{
    struct sk_buff *skb = NULL;
    void *msg_head = NULL;
    int rc = -1;
    int i;

    if(num_bind_process == 0) {
        /* no listening process */
        return FALSE;
    }

    for(i = 0 ; i < num_bind_process ; i++) {
        skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);

        if(skb) {
            msg_head = genlmsg_put(skb, 0, mtk_wifi_seqnum++, &mtk_wifi_gnl_family, 0, cmd);

            if(msg_head == NULL) {
                nlmsg_free(skb);
                return FALSE;
            }

            rc = nla_put_string(skb, MTK_WIFI_ATTR_MSG, aucMsg);
            if(rc != 0) {
                nlmsg_free(skb);
                return FALSE;
            }
        
            /* finalize the message */
            genlmsg_end(skb, msg_head);
        
            /* sending message */
            rc = genlmsg_unicast(&init_net, skb, bind_pid[i]);
            if(rc != 0) {
                return FALSE;
            }
        }
        else {
            return FALSE;
        }
    }

    return TRUE;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is called to identify PID for process binding
 *
 * @param   skb
 *          info
 *
 * @retval  0
 *          nonzero
 */
/*----------------------------------------------------------------------------*/
int mtk_wifi_bind(
    struct sk_buff *skb,
    struct genl_info *info
    )
{
    struct nlattr *na;
    char * mydata;

    if (info == NULL) {
        goto out;
    }

    /*for each attribute there is an index in info->attrs which points to a nlattr structure
     *in this structure the data is given
     */
    
    na = info->attrs[MTK_WIFI_ATTR_MSG];
    if (na) {
        mydata = (char *)nla_data(na);

        /* no need to parse mydata */
    }

    /* collect PID */
    if(num_bind_process < MAX_BIND_PROCESS) {
        bind_pid[num_bind_process] = info->snd_pid;
        num_bind_process++;
        }
    else {
        DBGLOG(INIT, WARN, ("%s(): exceeding binding limit %d\n", __func__, MAX_BIND_PROCESS));
    }

out:
    return 0;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is called for reset, shout not happen
 *
 * @param   skb
 *          info
 *
 * @retval  0
 *          nonzero
 */
/*----------------------------------------------------------------------------*/
int mtk_wifi_reset(void)
{   
    PARAM_CUSTOM_P2P_SET_STRUC_T p2pmode;
    struct net_device *netdev = NULL;
    DBGLOG(INIT, STATE, ("%s(): begin to reset WIFI\n", __func__));
	if (wlan_mode==1) //AP mode
		{
		 printk("Whole-chip reset:indicate power-off in AP mode\n");
		 power_state=0;
		}
	else if (wlan_mode==2)//P2P mode
		{
	     if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_on(WMTDRV_TYPE_WIFI)) {
                printk("Whole-chip reset:WMT turn on WIFI fail!\n");
             }
             else {
                printk("Whole-chip reset:WMT turn on WIFI OK!\n");
				msleep(300);
				printk("Whole-chip reset:begin to turn on P2P mode\n");
				netdev = dev_get_by_name(&init_net,"wlan0");
				p2pmode.u4Enable = 1;
			    p2pmode.u4Mode = 0;
			    pf_set_p2p_mode(netdev, p2pmode);
				printk("Whole-chip reset:turn on P2P mode\n");
				dev_put(netdev);
			    netdev = NULL;
					}
		 }

    return 0;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is called for generating reset request to WMT
 *
 * @param   None
 *
 * @retval  None
 */
/*----------------------------------------------------------------------------*/
VOID
glSendResetRequest(
    VOID
    )
{
    // WMT thread would trigger whole chip resetting itself
    return;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is called for checking if MT6620 is resetting
 *
 * @param   None
 *
 * @retval  TRUE
 *          FALSE
 */
/*----------------------------------------------------------------------------*/
BOOLEAN
kalIsResetting(
    VOID
    )
{
    return fgIsResetting;
}


#endif // CFG_CHIP_RESET_SUPPORT
