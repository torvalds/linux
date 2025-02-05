/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rvu

#if !defined(__RVU_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __RVU_TRACE_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/pci.h>

#include "mbox.h"

TRACE_EVENT(otx2_msg_alloc,
	    TP_PROTO(const struct pci_dev *pdev, u16 id, u64 size),
	    TP_ARGS(pdev, id, size),
	    TP_STRUCT__entry(__string(dev, pci_name(pdev))
			     __field(u16, id)
			     __field(u64, size)
	    ),
	    TP_fast_assign(__assign_str(dev);
			   __entry->id = id;
			   __entry->size = size;
	    ),
	    TP_printk("[%s] msg:(%s) size:%lld\n", __get_str(dev),
		      otx2_mbox_id2name(__entry->id), __entry->size)
);

TRACE_EVENT(otx2_msg_send,
	    TP_PROTO(const struct pci_dev *pdev, u16 num_msgs, u64 msg_size),
	    TP_ARGS(pdev, num_msgs, msg_size),
	    TP_STRUCT__entry(__string(dev, pci_name(pdev))
			     __field(u16, num_msgs)
			     __field(u64, msg_size)
	    ),
	    TP_fast_assign(__assign_str(dev);
			   __entry->num_msgs = num_msgs;
			   __entry->msg_size = msg_size;
	    ),
	    TP_printk("[%s] sent %d msg(s) of size:%lld\n", __get_str(dev),
		      __entry->num_msgs, __entry->msg_size)
);

TRACE_EVENT(otx2_msg_check,
	    TP_PROTO(const struct pci_dev *pdev, u16 reqid, u16 rspid, int rc),
	    TP_ARGS(pdev, reqid, rspid, rc),
	    TP_STRUCT__entry(__string(dev, pci_name(pdev))
			     __field(u16, reqid)
			     __field(u16, rspid)
			     __field(int, rc)
	    ),
	    TP_fast_assign(__assign_str(dev);
			   __entry->reqid = reqid;
			   __entry->rspid = rspid;
			   __entry->rc = rc;
	    ),
	    TP_printk("[%s] req->id:0x%x rsp->id:0x%x resp_code:%d\n",
		      __get_str(dev), __entry->reqid,
		      __entry->rspid, __entry->rc)
);

TRACE_EVENT(otx2_msg_interrupt,
	    TP_PROTO(const struct pci_dev *pdev, const char *msg, u64 intr),
	    TP_ARGS(pdev, msg, intr),
	    TP_STRUCT__entry(__string(dev, pci_name(pdev))
			     __string(str, msg)
			     __field(u64, intr)
	    ),
	    TP_fast_assign(__assign_str(dev);
			   __assign_str(str);
			   __entry->intr = intr;
	    ),
	    TP_printk("[%s] mbox interrupt %s (0x%llx)\n", __get_str(dev),
		      __get_str(str), __entry->intr)
);

TRACE_EVENT(otx2_msg_process,
	    TP_PROTO(const struct pci_dev *pdev, u16 id, int err),
	    TP_ARGS(pdev, id, err),
	    TP_STRUCT__entry(__string(dev, pci_name(pdev))
			     __field(u16, id)
			     __field(int, err)
	    ),
	    TP_fast_assign(__assign_str(dev);
			   __entry->id = id;
			   __entry->err = err;
	    ),
	    TP_printk("[%s] msg:(%s) error:%d\n", __get_str(dev),
		      otx2_mbox_id2name(__entry->id), __entry->err)
);

#endif /* __RVU_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE rvu_trace

#include <trace/define_trace.h>
