/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  FUJITSU Extended Socket Network Device driver
 *  Copyright (c) 2015-2016 FUJITSU LIMITED
 */

#if !defined(FJES_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define FJES_TRACE_H_

#include <linux/types.h>
#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM fjes

/* tracepoints for fjes_hw.c */

TRACE_EVENT(fjes_hw_issue_request_command,
	TP_PROTO(union REG_CR *cr, union REG_CS *cs, int timeout,
		 enum fjes_dev_command_response_e ret),
	TP_ARGS(cr, cs, timeout, ret),
	TP_STRUCT__entry(
		__field(u16, cr_req)
		__field(u8, cr_error)
		__field(u16, cr_err_info)
		__field(u8, cr_req_start)
		__field(u16, cs_req)
		__field(u8, cs_busy)
		__field(u8, cs_complete)
		__field(int, timeout)
		__field(int, ret)
	),
	TP_fast_assign(
		__entry->cr_req = cr->bits.req_code;
		__entry->cr_error = cr->bits.error;
		__entry->cr_err_info = cr->bits.err_info;
		__entry->cr_req_start = cr->bits.req_start;
		__entry->cs_req = cs->bits.req_code;
		__entry->cs_busy = cs->bits.busy;
		__entry->cs_complete = cs->bits.complete;
		__entry->timeout = timeout;
		__entry->ret = ret;
	),
	TP_printk("CR=[req=%04x, error=%u, err_info=%04x, req_start=%u], CS=[req=%04x, busy=%u, complete=%u], timeout=%d, ret=%d",
		  __entry->cr_req, __entry->cr_error, __entry->cr_err_info,
		  __entry->cr_req_start, __entry->cs_req, __entry->cs_busy,
		  __entry->cs_complete, __entry->timeout, __entry->ret)
);

TRACE_EVENT(fjes_hw_request_info,
	TP_PROTO(struct fjes_hw *hw, union fjes_device_command_res *res_buf),
	TP_ARGS(hw, res_buf),
	TP_STRUCT__entry(
		__field(int, length)
		__field(int, code)
		__dynamic_array(u8, zone, hw->max_epid)
		__dynamic_array(u8, status, hw->max_epid)
	),
	TP_fast_assign(
		int x;

		__entry->length = res_buf->info.length;
		__entry->code = res_buf->info.code;
		for (x = 0; x < hw->max_epid; x++) {
			*((u8 *)__get_dynamic_array(zone) + x) =
					res_buf->info.info[x].zone;
			*((u8 *)__get_dynamic_array(status) + x) =
					res_buf->info.info[x].es_status;
		}
	),
	TP_printk("res_buf=[length=%d, code=%d, es_zones=%s, es_status=%s]",
		  __entry->length, __entry->code,
		  __print_array(__get_dynamic_array(zone),
				__get_dynamic_array_len(zone) / sizeof(u8),
				sizeof(u8)),
		  __print_array(__get_dynamic_array(status),
				__get_dynamic_array_len(status) / sizeof(u8),
				sizeof(u8)))
);

TRACE_EVENT(fjes_hw_request_info_err,
	TP_PROTO(char *err),
	TP_ARGS(err),
	TP_STRUCT__entry(
		__string(err, err)
	),
	TP_fast_assign(
		__assign_str(err, err);
	),
	TP_printk("%s", __get_str(err))
);

TRACE_EVENT(fjes_hw_register_buff_addr_req,
	TP_PROTO(union fjes_device_command_req *req_buf,
		 struct ep_share_mem_info *buf_pair),
	TP_ARGS(req_buf, buf_pair),
	TP_STRUCT__entry(
		__field(int, length)
		__field(int, epid)
		__field(u64, tx)
		__field(size_t,	tx_size)
		__field(u64, rx)
		__field(size_t,	rx_size)
	),
	TP_fast_assign(
		void *tx, *rx;

		tx = (void *)buf_pair->tx.buffer;
		rx = (void *)buf_pair->rx.buffer;
		__entry->length = req_buf->share_buffer.length;
		__entry->epid = req_buf->share_buffer.epid;
		__entry->tx_size = buf_pair->tx.size;
		__entry->rx_size = buf_pair->rx.size;
		__entry->tx = page_to_phys(vmalloc_to_page(tx)) +
				offset_in_page(tx);
		__entry->rx = page_to_phys(vmalloc_to_page(rx)) +
				offset_in_page(rx);
	),
	TP_printk("req_buf=[length=%d, epid=%d], TX=[phy=0x%016llx, size=%zu], RX=[phy=0x%016llx, size=%zu]",
		  __entry->length, __entry->epid, __entry->tx, __entry->tx_size,
		  __entry->rx, __entry->rx_size)
);

TRACE_EVENT(fjes_hw_register_buff_addr,
	TP_PROTO(union fjes_device_command_res *res_buf, int timeout),
	TP_ARGS(res_buf, timeout),
	TP_STRUCT__entry(
		__field(int, length)
		__field(int, code)
		__field(int, timeout)
	),
	TP_fast_assign(
		__entry->length = res_buf->share_buffer.length;
		__entry->code = res_buf->share_buffer.code;
		__entry->timeout = timeout;
	),
	TP_printk("res_buf=[length=%d, code=%d], timeout=%d",
		  __entry->length, __entry->code, __entry->timeout)
);

TRACE_EVENT(fjes_hw_register_buff_addr_err,
	TP_PROTO(char *err),
	TP_ARGS(err),
	TP_STRUCT__entry(
		__string(err, err)
	),
	TP_fast_assign(
		__assign_str(err, err);
	),
	TP_printk("%s", __get_str(err))
);

TRACE_EVENT(fjes_hw_unregister_buff_addr_req,
	TP_PROTO(union fjes_device_command_req *req_buf),
	TP_ARGS(req_buf),
	TP_STRUCT__entry(
		__field(int, length)
		__field(int, epid)
	),
	TP_fast_assign(
		__entry->length = req_buf->unshare_buffer.length;
		__entry->epid = req_buf->unshare_buffer.epid;
	),
	TP_printk("req_buf=[length=%d, epid=%d]",
		  __entry->length, __entry->epid)
);

TRACE_EVENT(fjes_hw_unregister_buff_addr,
	TP_PROTO(union fjes_device_command_res *res_buf, int timeout),
	TP_ARGS(res_buf, timeout),
	TP_STRUCT__entry(
		__field(int, length)
		__field(int, code)
		__field(int, timeout)
	),
	TP_fast_assign(
		__entry->length = res_buf->unshare_buffer.length;
		__entry->code = res_buf->unshare_buffer.code;
		__entry->timeout = timeout;
	),
	TP_printk("res_buf=[length=%d, code=%d], timeout=%d",
		  __entry->length, __entry->code, __entry->timeout)
);

TRACE_EVENT(fjes_hw_unregister_buff_addr_err,
	TP_PROTO(char *err),
	TP_ARGS(err),
	TP_STRUCT__entry(
		__string(err, err)
	),
	TP_fast_assign(
		__assign_str(err, err);
	),
	TP_printk("%s", __get_str(err))
);

TRACE_EVENT(fjes_hw_start_debug_req,
	TP_PROTO(union fjes_device_command_req *req_buf),
	TP_ARGS(req_buf),
	TP_STRUCT__entry(
		__field(int, length)
		__field(int, mode)
		__field(phys_addr_t, buffer)
	),
	TP_fast_assign(
		__entry->length = req_buf->start_trace.length;
		__entry->mode = req_buf->start_trace.mode;
		__entry->buffer = req_buf->start_trace.buffer[0];
	),
	TP_printk("req_buf=[length=%d, mode=%d, buffer=%pap]",
		  __entry->length, __entry->mode, &__entry->buffer)
);

TRACE_EVENT(fjes_hw_start_debug,
	TP_PROTO(union fjes_device_command_res *res_buf),
	TP_ARGS(res_buf),
	TP_STRUCT__entry(
		__field(int, length)
		__field(int, code)
	),
	TP_fast_assign(
		__entry->length = res_buf->start_trace.length;
		__entry->code = res_buf->start_trace.code;
	),
	TP_printk("res_buf=[length=%d, code=%d]", __entry->length, __entry->code)
);

TRACE_EVENT(fjes_hw_start_debug_err,
	TP_PROTO(char *err),
	TP_ARGS(err),
	TP_STRUCT__entry(
		 __string(err, err)
	),
	TP_fast_assign(
		__assign_str(err, err);
	),
	TP_printk("%s", __get_str(err))
);

TRACE_EVENT(fjes_hw_stop_debug,
	TP_PROTO(union fjes_device_command_res *res_buf),
	TP_ARGS(res_buf),
	TP_STRUCT__entry(
		__field(int, length)
		__field(int, code)
	),
	TP_fast_assign(
		__entry->length = res_buf->stop_trace.length;
		__entry->code = res_buf->stop_trace.code;
	),
	TP_printk("res_buf=[length=%d, code=%d]", __entry->length, __entry->code)
);

TRACE_EVENT(fjes_hw_stop_debug_err,
	TP_PROTO(char *err),
	TP_ARGS(err),
	TP_STRUCT__entry(
		 __string(err, err)
	),
	TP_fast_assign(
		__assign_str(err, err);
	),
	TP_printk("%s", __get_str(err))
);

/* tracepoints for fjes_main.c */

TRACE_EVENT(fjes_txrx_stop_req_irq_pre,
	TP_PROTO(struct fjes_hw *hw, int src_epid,
		 enum ep_partner_status status),
	TP_ARGS(hw, src_epid, status),
	TP_STRUCT__entry(
		__field(int, src_epid)
		__field(enum ep_partner_status, status)
		__field(u8, ep_status)
		__field(unsigned long, txrx_stop_req_bit)
		__field(u16, rx_status)
	),
	TP_fast_assign(
		__entry->src_epid = src_epid;
		__entry->status = status;
		__entry->ep_status = hw->hw_info.share->ep_status[src_epid];
		__entry->txrx_stop_req_bit = hw->txrx_stop_req_bit;
		__entry->rx_status =
			hw->ep_shm_info[src_epid].tx.info->v1i.rx_status;
	),
	TP_printk("epid=%d, partner_status=%d, ep_status=%x, txrx_stop_req_bit=%016lx, tx.rx_status=%08x",
		  __entry->src_epid, __entry->status, __entry->ep_status,
		  __entry->txrx_stop_req_bit, __entry->rx_status)
);

TRACE_EVENT(fjes_txrx_stop_req_irq_post,
	TP_PROTO(struct fjes_hw *hw, int src_epid),
	TP_ARGS(hw, src_epid),
	TP_STRUCT__entry(
		__field(int, src_epid)
		__field(u8, ep_status)
		__field(unsigned long, txrx_stop_req_bit)
		__field(u16, rx_status)
	),
	TP_fast_assign(
		__entry->src_epid = src_epid;
		__entry->ep_status = hw->hw_info.share->ep_status[src_epid];
		__entry->txrx_stop_req_bit = hw->txrx_stop_req_bit;
		__entry->rx_status = hw->ep_shm_info[src_epid].tx.info->v1i.rx_status;
	),
	TP_printk("epid=%d, ep_status=%x, txrx_stop_req_bit=%016lx, tx.rx_status=%08x",
		  __entry->src_epid, __entry->ep_status,
		  __entry->txrx_stop_req_bit, __entry->rx_status)
);

TRACE_EVENT(fjes_stop_req_irq_pre,
	TP_PROTO(struct fjes_hw *hw, int src_epid,
		 enum ep_partner_status status),
	TP_ARGS(hw, src_epid, status),
	TP_STRUCT__entry(
		__field(int, src_epid)
		__field(enum ep_partner_status, status)
		__field(u8, ep_status)
		__field(unsigned long, txrx_stop_req_bit)
		__field(u16, rx_status)
	),
	TP_fast_assign(
		__entry->src_epid = src_epid;
		__entry->status = status;
		__entry->ep_status = hw->hw_info.share->ep_status[src_epid];
		__entry->txrx_stop_req_bit = hw->txrx_stop_req_bit;
		__entry->rx_status =
			hw->ep_shm_info[src_epid].tx.info->v1i.rx_status;
	),
	TP_printk("epid=%d, partner_status=%d, ep_status=%x, txrx_stop_req_bit=%016lx, tx.rx_status=%08x",
		  __entry->src_epid, __entry->status, __entry->ep_status,
		  __entry->txrx_stop_req_bit, __entry->rx_status)
);

TRACE_EVENT(fjes_stop_req_irq_post,
	TP_PROTO(struct fjes_hw *hw, int src_epid),
	TP_ARGS(hw, src_epid),
	TP_STRUCT__entry(
		__field(int, src_epid)
		__field(u8, ep_status)
		__field(unsigned long, txrx_stop_req_bit)
		__field(u16, rx_status)
	),
	TP_fast_assign(
		__entry->src_epid = src_epid;
		__entry->ep_status = hw->hw_info.share->ep_status[src_epid];
		__entry->txrx_stop_req_bit = hw->txrx_stop_req_bit;
		__entry->rx_status =
			hw->ep_shm_info[src_epid].tx.info->v1i.rx_status;
	),
	TP_printk("epid=%d, ep_status=%x, txrx_stop_req_bit=%016lx, tx.rx_status=%08x",
		  __entry->src_epid, __entry->ep_status,
		  __entry->txrx_stop_req_bit, __entry->rx_status)
);

#endif /* FJES_TRACE_H_ */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../../drivers/net/fjes
#define TRACE_INCLUDE_FILE fjes_trace

/* This part must be outside protection */
#include <trace/define_trace.h>
