/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2018-2020 Hisilicon Limited. */

/* This must be outside ifdef _HCLGE_TRACE_H */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hns3

#if !defined(_HCLGE_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _HCLGE_TRACE_H_

#include <linux/tracepoint.h>

#define PF_DESC_LEN	(sizeof(struct hclge_desc) / sizeof(u32))
#define PF_GET_MBX_LEN	(sizeof(struct hclge_mbx_vf_to_pf_cmd) / sizeof(u32))
#define PF_SEND_MBX_LEN	(sizeof(struct hclge_mbx_pf_to_vf_cmd) / sizeof(u32))

TRACE_EVENT(hclge_pf_mbx_get,
	TP_PROTO(
		struct hclge_dev *hdev,
		struct hclge_mbx_vf_to_pf_cmd *req),
	TP_ARGS(hdev, req),

	TP_STRUCT__entry(
		__field(u8, vfid)
		__field(u8, code)
		__field(u8, subcode)
		__string(pciname, pci_name(hdev->pdev))
		__string(devname, hdev->vport[0].nic.kinfo.netdev->name)
		__array(u32, mbx_data, PF_GET_MBX_LEN)
	),

	TP_fast_assign(
		__entry->vfid = req->mbx_src_vfid;
		__entry->code = req->msg.code;
		__entry->subcode = req->msg.subcode;
		__assign_str(pciname);
		__assign_str(devname);
		memcpy(__entry->mbx_data, req,
		       sizeof(struct hclge_mbx_vf_to_pf_cmd));
	),

	TP_printk(
		"%s %s vfid:%u code:%u subcode:%u data:%s",
		__get_str(pciname), __get_str(devname), __entry->vfid,
		__entry->code, __entry->subcode,
		__print_array(__entry->mbx_data, PF_GET_MBX_LEN, sizeof(u32))
	)
);

TRACE_EVENT(hclge_pf_mbx_send,
	TP_PROTO(
		struct hclge_dev *hdev,
		struct hclge_mbx_pf_to_vf_cmd *req),
	TP_ARGS(hdev, req),

	TP_STRUCT__entry(
		__field(u8, vfid)
		__field(u16, code)
		__string(pciname, pci_name(hdev->pdev))
		__string(devname, hdev->vport[0].nic.kinfo.netdev->name)
		__array(u32, mbx_data, PF_SEND_MBX_LEN)
	),

	TP_fast_assign(
		__entry->vfid = req->dest_vfid;
		__entry->code = le16_to_cpu(req->msg.code);
		__assign_str(pciname);
		__assign_str(devname);
		memcpy(__entry->mbx_data, req,
		       sizeof(struct hclge_mbx_pf_to_vf_cmd));
	),

	TP_printk(
		"%s %s vfid:%u code:%u data:%s",
		__get_str(pciname), __get_str(devname), __entry->vfid,
		__entry->code,
		__print_array(__entry->mbx_data, PF_SEND_MBX_LEN, sizeof(u32))
	)
);

DECLARE_EVENT_CLASS(hclge_pf_cmd_template,
		    TP_PROTO(struct hclge_comm_hw *hw,
			     struct hclge_desc *desc,
			     int index,
			     int num),
		    TP_ARGS(hw, desc, index, num),

		    TP_STRUCT__entry(__field(u16, opcode)
			__field(u16, flag)
			__field(u16, retval)
			__field(u16, rsv)
			__field(int, index)
			__field(int, num)
			__string(pciname, pci_name(hw->cmq.csq.pdev))
			__array(u32, data, HCLGE_DESC_DATA_LEN)),

		    TP_fast_assign(int i;
			__entry->opcode = le16_to_cpu(desc->opcode);
			__entry->flag = le16_to_cpu(desc->flag);
			__entry->retval = le16_to_cpu(desc->retval);
			__entry->rsv = le16_to_cpu(desc->rsv);
			__entry->index = index;
			__entry->num = num;
			__assign_str(pciname);
			for (i = 0; i < HCLGE_DESC_DATA_LEN; i++)
				__entry->data[i] = le32_to_cpu(desc->data[i]);),

		    TP_printk("%s opcode:0x%04x %d-%d flag:0x%04x retval:0x%04x rsv:0x%04x data:%s",
			      __get_str(pciname), __entry->opcode,
			      __entry->index, __entry->num,
			      __entry->flag, __entry->retval, __entry->rsv,
			      __print_array(__entry->data,
					    HCLGE_DESC_DATA_LEN, sizeof(u32)))
);

DEFINE_EVENT(hclge_pf_cmd_template, hclge_pf_cmd_send,
	     TP_PROTO(struct hclge_comm_hw *hw,
		      struct hclge_desc *desc,
		      int index,
		      int num),
	     TP_ARGS(hw, desc, index, num)
);

DEFINE_EVENT(hclge_pf_cmd_template, hclge_pf_cmd_get,
	     TP_PROTO(struct hclge_comm_hw *hw,
		      struct hclge_desc *desc,
		      int index,
		      int num),
	     TP_ARGS(hw, desc, index, num)
);

DECLARE_EVENT_CLASS(hclge_pf_special_cmd_template,
		    TP_PROTO(struct hclge_comm_hw *hw,
			     __le32 *data,
			     int index,
			     int num),
		    TP_ARGS(hw, data, index, num),

		    TP_STRUCT__entry(__field(int, index)
			__field(int, num)
			__string(pciname, pci_name(hw->cmq.csq.pdev))
			__array(u32, data, PF_DESC_LEN)),

		    TP_fast_assign(int i;
			__entry->index = index;
			__entry->num = num;
			__assign_str(pciname);
			for (i = 0; i < PF_DESC_LEN; i++)
				__entry->data[i] = le32_to_cpu(data[i]);
		),

		    TP_printk("%s %d-%d data:%s",
			      __get_str(pciname),
			      __entry->index, __entry->num,
			      __print_array(__entry->data,
					    PF_DESC_LEN, sizeof(u32)))
);

DEFINE_EVENT(hclge_pf_special_cmd_template, hclge_pf_special_cmd_send,
	     TP_PROTO(struct hclge_comm_hw *hw,
		      __le32 *desc,
		      int index,
		      int num),
	     TP_ARGS(hw, desc, index, num));

DEFINE_EVENT(hclge_pf_special_cmd_template, hclge_pf_special_cmd_get,
	     TP_PROTO(struct hclge_comm_hw *hw,
		      __le32 *desc,
		      int index,
		      int num),
	     TP_ARGS(hw, desc, index, num)
);

#endif /* _HCLGE_TRACE_H_ */

/* This must be outside ifdef _HCLGE_TRACE_H */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE hclge_trace
#include <trace/define_trace.h>
