/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2016-2017 Hisilicon Limited. */

#ifndef __HCLGE_MBX_H
#define __HCLGE_MBX_H
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/types.h>

#define HCLGE_MBX_VF_MSG_DATA_NUM	16

enum HCLGE_MBX_OPCODE {
	HCLGE_MBX_RESET = 0x01,		/* (VF -> PF) assert reset */
	HCLGE_MBX_ASSERTING_RESET,	/* (PF -> VF) PF is asserting reset*/
	HCLGE_MBX_SET_UNICAST,		/* (VF -> PF) set UC addr */
	HCLGE_MBX_SET_MULTICAST,	/* (VF -> PF) set MC addr */
	HCLGE_MBX_SET_VLAN,		/* (VF -> PF) set VLAN */
	HCLGE_MBX_MAP_RING_TO_VECTOR,	/* (VF -> PF) map ring-to-vector */
	HCLGE_MBX_UNMAP_RING_TO_VECTOR,	/* (VF -> PF) unamp ring-to-vector */
	HCLGE_MBX_SET_PROMISC_MODE,	/* (VF -> PF) set promiscuous mode */
	HCLGE_MBX_SET_MACVLAN,		/* (VF -> PF) set unicast filter */
	HCLGE_MBX_API_NEGOTIATE,	/* (VF -> PF) negotiate API version */
	HCLGE_MBX_GET_QINFO,		/* (VF -> PF) get queue config */
	HCLGE_MBX_GET_QDEPTH,		/* (VF -> PF) get queue depth */
	HCLGE_MBX_GET_TCINFO,		/* (VF -> PF) get TC config */
	HCLGE_MBX_GET_RETA,		/* (VF -> PF) get RETA */
	HCLGE_MBX_GET_RSS_KEY,		/* (VF -> PF) get RSS key */
	HCLGE_MBX_GET_MAC_ADDR,		/* (VF -> PF) get MAC addr */
	HCLGE_MBX_PF_VF_RESP,		/* (PF -> VF) generate respone to VF */
	HCLGE_MBX_GET_BDNUM,		/* (VF -> PF) get BD num */
	HCLGE_MBX_GET_BUFSIZE,		/* (VF -> PF) get buffer size */
	HCLGE_MBX_GET_STREAMID,		/* (VF -> PF) get stream id */
	HCLGE_MBX_SET_AESTART,		/* (VF -> PF) start ae */
	HCLGE_MBX_SET_TSOSTATS,		/* (VF -> PF) get tso stats */
	HCLGE_MBX_LINK_STAT_CHANGE,	/* (PF -> VF) link status has changed */
	HCLGE_MBX_GET_BASE_CONFIG,	/* (VF -> PF) get config */
	HCLGE_MBX_BIND_FUNC_QUEUE,	/* (VF -> PF) bind function and queue */
	HCLGE_MBX_GET_LINK_STATUS,	/* (VF -> PF) get link status */
	HCLGE_MBX_QUEUE_RESET,		/* (VF -> PF) reset queue */
	HCLGE_MBX_KEEP_ALIVE,		/* (VF -> PF) send keep alive cmd */
	HCLGE_MBX_SET_ALIVE,		/* (VF -> PF) set alive state */
	HCLGE_MBX_SET_MTU,		/* (VF -> PF) set mtu */
	HCLGE_MBX_GET_QID_IN_PF,	/* (VF -> PF) get queue id in pf */
	HCLGE_MBX_LINK_STAT_MODE,	/* (PF -> VF) link mode has changed */
	HCLGE_MBX_GET_LINK_MODE,	/* (VF -> PF) get the link mode of pf */
	HCLGE_MBX_PUSH_VLAN_INFO,	/* (PF -> VF) push port base vlan */
	HCLGE_MBX_GET_MEDIA_TYPE,       /* (VF -> PF) get media type */
	HCLGE_MBX_PUSH_PROMISC_INFO,	/* (PF -> VF) push vf promisc info */
	HCLGE_MBX_VF_UNINIT,            /* (VF -> PF) vf is unintializing */

	HCLGE_MBX_GET_VF_FLR_STATUS = 200, /* (M7 -> PF) get vf flr status */
	HCLGE_MBX_PUSH_LINK_STATUS,	/* (M7 -> PF) get port link status */
	HCLGE_MBX_NCSI_ERROR,		/* (M7 -> PF) receive a NCSI error */
};

/* below are per-VF mac-vlan subcodes */
enum hclge_mbx_mac_vlan_subcode {
	HCLGE_MBX_MAC_VLAN_UC_MODIFY = 0,	/* modify UC mac addr */
	HCLGE_MBX_MAC_VLAN_UC_ADD,		/* add a new UC mac addr */
	HCLGE_MBX_MAC_VLAN_UC_REMOVE,		/* remove a new UC mac addr */
	HCLGE_MBX_MAC_VLAN_MC_MODIFY,		/* modify MC mac addr */
	HCLGE_MBX_MAC_VLAN_MC_ADD,		/* add new MC mac addr */
	HCLGE_MBX_MAC_VLAN_MC_REMOVE,		/* remove MC mac addr */
};

/* below are per-VF vlan cfg subcodes */
enum hclge_mbx_vlan_cfg_subcode {
	HCLGE_MBX_VLAN_FILTER = 0,	/* set vlan filter */
	HCLGE_MBX_VLAN_TX_OFF_CFG,	/* set tx side vlan offload */
	HCLGE_MBX_VLAN_RX_OFF_CFG,	/* set rx side vlan offload */
	HCLGE_MBX_PORT_BASE_VLAN_CFG,	/* set port based vlan configuration */
	HCLGE_MBX_GET_PORT_BASE_VLAN_STATE,	/* get port based vlan state */
};

#define HCLGE_MBX_MAX_MSG_SIZE	16
#define HCLGE_MBX_MAX_RESP_DATA_SIZE	8U
#define HCLGE_MBX_RING_MAP_BASIC_MSG_NUM	3
#define HCLGE_MBX_RING_NODE_VARIABLE_NUM	3

struct hclgevf_mbx_resp_status {
	struct mutex mbx_mutex; /* protects against contending sync cmd resp */
	u32 origin_mbx_msg;
	bool received_resp;
	int resp_status;
	u8 additional_info[HCLGE_MBX_MAX_RESP_DATA_SIZE];
};

struct hclge_mbx_vf_to_pf_cmd {
	u8 rsv;
	u8 mbx_src_vfid; /* Auto filled by IMP */
	u8 mbx_need_resp;
	u8 rsv1[1];
	u8 msg_len;
	u8 rsv2[3];
	u8 msg[HCLGE_MBX_MAX_MSG_SIZE];
};

#define HCLGE_MBX_NEED_RESP_BIT		BIT(0)

struct hclge_mbx_pf_to_vf_cmd {
	u8 dest_vfid;
	u8 rsv[3];
	u8 msg_len;
	u8 rsv1[3];
	u16 msg[8];
};

struct hclge_vf_rst_cmd {
	u8 dest_vfid;
	u8 vf_rst;
	u8 rsv[22];
};

/* used by VF to store the received Async responses from PF */
struct hclgevf_mbx_arq_ring {
#define HCLGE_MBX_MAX_ARQ_MSG_SIZE	8
#define HCLGE_MBX_MAX_ARQ_MSG_NUM	1024
	struct hclgevf_dev *hdev;
	u32 head;
	u32 tail;
	atomic_t count;
	u16 msg_q[HCLGE_MBX_MAX_ARQ_MSG_NUM][HCLGE_MBX_MAX_ARQ_MSG_SIZE];
};

#define hclge_mbx_ring_ptr_move_crq(crq) \
	(crq->next_to_use = (crq->next_to_use + 1) % crq->desc_num)
#define hclge_mbx_tail_ptr_move_arq(arq) \
	(arq.tail = (arq.tail + 1) % HCLGE_MBX_MAX_ARQ_MSG_SIZE)
#define hclge_mbx_head_ptr_move_arq(arq) \
		(arq.head = (arq.head + 1) % HCLGE_MBX_MAX_ARQ_MSG_SIZE)
#endif
