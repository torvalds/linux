/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2018-2019 Hisilicon Limited. */

/* This must be outside ifdef _HCLGEVF_TRACE_H */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hns3

#if !defined(_HCLGEVF_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _HCLGEVF_TRACE_H_

#include <linux/tracepoint.h>

#define VF_GET_MBX_LEN	(sizeof(struct hclge_mbx_pf_to_vf_cmd) / sizeof(u32))
#define VF_SEND_MBX_LEN	(sizeof(struct hclge_mbx_vf_to_pf_cmd) / sizeof(u32))

TRACE_EVENT(hclge_vf_mbx_get,
	TP_PROTO(
		struct hclgevf_dev *hdev,
		struct hclge_mbx_pf_to_vf_cmd *req),
	TP_ARGS(hdev, req),

	TP_STRUCT__entry(
		__field(u8, vfid)
		__field(u16, code)
		__string(pciname, pci_name(hdev->pdev))
		__string(devname, &hdev->nic.kinfo.netdev->name)
		__array(u32, mbx_data, VF_GET_MBX_LEN)
	),

	TP_fast_assign(
		__entry->vfid = req->dest_vfid;
		__entry->code = le16_to_cpu(req->msg.code);
		__assign_str(pciname, pci_name(hdev->pdev));
		__assign_str(devname, &hdev->nic.kinfo.netdev->name);
		memcpy(__entry->mbx_data, req,
		       sizeof(struct hclge_mbx_pf_to_vf_cmd));
	),

	TP_printk(
		"%s %s vfid:%u code:%u data:%s",
		__get_str(pciname), __get_str(devname), __entry->vfid,
		__entry->code,
		__print_array(__entry->mbx_data, VF_GET_MBX_LEN, sizeof(u32))
	)
);

TRACE_EVENT(hclge_vf_mbx_send,
	TP_PROTO(
		struct hclgevf_dev *hdev,
		struct hclge_mbx_vf_to_pf_cmd *req),
	TP_ARGS(hdev, req),

	TP_STRUCT__entry(
		__field(u8, vfid)
		__field(u8, code)
		__field(u8, subcode)
		__string(pciname, pci_name(hdev->pdev))
		__string(devname, &hdev->nic.kinfo.netdev->name)
		__array(u32, mbx_data, VF_SEND_MBX_LEN)
	),

	TP_fast_assign(
		__entry->vfid = req->mbx_src_vfid;
		__entry->code = req->msg.code;
		__entry->subcode = req->msg.subcode;
		__assign_str(pciname, pci_name(hdev->pdev));
		__assign_str(devname, &hdev->nic.kinfo.netdev->name);
		memcpy(__entry->mbx_data, req,
		       sizeof(struct hclge_mbx_vf_to_pf_cmd));
	),

	TP_printk(
		"%s %s vfid:%u code:%u subcode:%u data:%s",
		__get_str(pciname), __get_str(devname), __entry->vfid,
		__entry->code, __entry->subcode,
		__print_array(__entry->mbx_data, VF_SEND_MBX_LEN, sizeof(u32))
	)
);

#endif /* _HCLGEVF_TRACE_H_ */

/* This must be outside ifdef _HCLGEVF_TRACE_H */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE hclgevf_trace
#include <trace/define_trace.h>
