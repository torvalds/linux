#if !defined(__IWM_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __IWM_TRACE_H__

#include <linux/tracepoint.h>

#if !defined(CONFIG_IWM_TRACING)
#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, ...) \
static inline void trace_ ## name(proto) {}
#endif

#undef TRACE_SYSTEM
#define TRACE_SYSTEM iwm

#define IWM_ENTRY	__array(char, ndev_name, 16)
#define IWM_ASSIGN	strlcpy(__entry->ndev_name, iwm_to_ndev(iwm)->name, 16)
#define IWM_PR_FMT	"%s"
#define IWM_PR_ARG	__entry->ndev_name

TRACE_EVENT(iwm_tx_nonwifi_cmd,
	TP_PROTO(struct iwm_priv *iwm, struct iwm_udma_out_nonwifi_hdr *hdr),

	TP_ARGS(iwm, hdr),

	TP_STRUCT__entry(
		IWM_ENTRY
		__field(u8, opcode)
		__field(u8, resp)
		__field(u8, eot)
		__field(u8, hw)
		__field(u16, seq)
		__field(u32, addr)
		__field(u32, op1)
		__field(u32, op2)
	),

	TP_fast_assign(
		IWM_ASSIGN;
		__entry->opcode = GET_VAL32(hdr->cmd, UMAC_HDI_OUT_CMD_OPCODE);
		__entry->resp = GET_VAL32(hdr->cmd, UDMA_HDI_OUT_NW_CMD_RESP);
		__entry->eot = GET_VAL32(hdr->cmd, UMAC_HDI_OUT_CMD_EOT);
		__entry->hw = GET_VAL32(hdr->cmd, UDMA_HDI_OUT_NW_CMD_HANDLE_BY_HW);
		__entry->seq = GET_VAL32(hdr->cmd, UDMA_HDI_OUT_CMD_NON_WIFI_HW_SEQ_NUM);
		__entry->addr = le32_to_cpu(hdr->addr);
		__entry->op1 = le32_to_cpu(hdr->op1_sz);
		__entry->op2 = le32_to_cpu(hdr->op2);
	),

	TP_printk(
		IWM_PR_FMT " Tx TARGET CMD: opcode 0x%x, resp %d, eot %d, "
		"hw %d, seq 0x%x, addr 0x%x, op1 0x%x, op2 0x%x",
		IWM_PR_ARG, __entry->opcode, __entry->resp, __entry->eot,
		__entry->hw, __entry->seq, __entry->addr, __entry->op1,
		__entry->op2
	)
);

TRACE_EVENT(iwm_tx_wifi_cmd,
	TP_PROTO(struct iwm_priv *iwm, struct iwm_umac_wifi_out_hdr *hdr),

	TP_ARGS(iwm, hdr),

	TP_STRUCT__entry(
		IWM_ENTRY
		__field(u8, opcode)
		__field(u8, lmac)
		__field(u8, resp)
		__field(u8, eot)
		__field(u8, ra_tid)
		__field(u8, credit_group)
		__field(u8, color)
		__field(u16, seq)
	),

	TP_fast_assign(
		IWM_ASSIGN;
		__entry->opcode = hdr->sw_hdr.cmd.cmd;
		__entry->lmac = 0;
		__entry->seq = __le16_to_cpu(hdr->sw_hdr.cmd.seq_num);
		__entry->resp = GET_VAL8(hdr->sw_hdr.cmd.flags, UMAC_DEV_CMD_FLAGS_RESP_REQ);
		__entry->color = GET_VAL32(hdr->sw_hdr.meta_data, UMAC_FW_CMD_TX_STA_COLOR);
		__entry->eot = GET_VAL32(hdr->hw_hdr.cmd, UMAC_HDI_OUT_CMD_EOT);
		__entry->ra_tid = GET_VAL32(hdr->hw_hdr.meta_data, UMAC_HDI_OUT_RATID);
		__entry->credit_group = GET_VAL32(hdr->hw_hdr.meta_data, UMAC_HDI_OUT_CREDIT_GRP);
		if (__entry->opcode == UMAC_CMD_OPCODE_WIFI_PASS_THROUGH ||
		    __entry->opcode == UMAC_CMD_OPCODE_WIFI_IF_WRAPPER) {
			__entry->lmac = 1;
			__entry->opcode = ((struct iwm_lmac_hdr *)(hdr + 1))->id;
		}
	),

	TP_printk(
		IWM_PR_FMT " Tx %cMAC CMD: opcode 0x%x, resp %d, eot %d, "
		"seq 0x%x, sta_color 0x%x, ra_tid 0x%x, credit_group 0x%x",
		IWM_PR_ARG, __entry->lmac ? 'L' : 'U', __entry->opcode,
		__entry->resp, __entry->eot, __entry->seq, __entry->color,
		__entry->ra_tid, __entry->credit_group
	)
);

TRACE_EVENT(iwm_tx_packets,
	TP_PROTO(struct iwm_priv *iwm, u8 *buf, int len),

	TP_ARGS(iwm, buf, len),

	TP_STRUCT__entry(
		IWM_ENTRY
		__field(u8, eot)
		__field(u8, ra_tid)
		__field(u8, credit_group)
		__field(u8, color)
		__field(u16, seq)
		__field(u8, npkt)
		__field(u32, bytes)
	),

	TP_fast_assign(
		struct iwm_umac_wifi_out_hdr *hdr =
			(struct iwm_umac_wifi_out_hdr *)buf;

		IWM_ASSIGN;
		__entry->eot = GET_VAL32(hdr->hw_hdr.cmd, UMAC_HDI_OUT_CMD_EOT);
		__entry->ra_tid = GET_VAL32(hdr->hw_hdr.meta_data, UMAC_HDI_OUT_RATID);
		__entry->credit_group = GET_VAL32(hdr->hw_hdr.meta_data, UMAC_HDI_OUT_CREDIT_GRP);
		__entry->color = GET_VAL32(hdr->sw_hdr.meta_data, UMAC_FW_CMD_TX_STA_COLOR);
		__entry->seq = __le16_to_cpu(hdr->sw_hdr.cmd.seq_num);
		__entry->npkt = 1;
		__entry->bytes = len;

		if (!__entry->eot) {
			int count;
			u8 *ptr = buf;

			__entry->npkt = 0;
			while (ptr < buf + len) {
				count = GET_VAL32(hdr->sw_hdr.meta_data,
						  UMAC_FW_CMD_BYTE_COUNT);
				ptr += ALIGN(sizeof(*hdr) + count, 16);
				hdr = (struct iwm_umac_wifi_out_hdr *)ptr;
				__entry->npkt++;
			}
		}
	),

	TP_printk(
		IWM_PR_FMT " Tx %spacket: eot %d, seq 0x%x, sta_color 0x%x, "
		"ra_tid 0x%x, credit_group 0x%x, embeded_packets %d, %d bytes",
		IWM_PR_ARG, !__entry->eot ? "concatenated " : "",
		__entry->eot, __entry->seq, __entry->color, __entry->ra_tid,
		__entry->credit_group, __entry->npkt, __entry->bytes
	)
);

TRACE_EVENT(iwm_rx_nonwifi_cmd,
	TP_PROTO(struct iwm_priv *iwm, void *buf, int len),

	TP_ARGS(iwm, buf, len),

	TP_STRUCT__entry(
		IWM_ENTRY
		__field(u8, opcode)
		__field(u16, seq)
		__field(u32, len)
	),

	TP_fast_assign(
		struct iwm_udma_in_hdr *hdr = buf;

		IWM_ASSIGN;
		__entry->opcode = GET_VAL32(hdr->cmd, UDMA_HDI_IN_NW_CMD_OPCODE);
		__entry->seq = GET_VAL32(hdr->cmd, UDMA_HDI_IN_CMD_NON_WIFI_HW_SEQ_NUM);
		__entry->len = len;
	),

	TP_printk(
		IWM_PR_FMT " Rx TARGET RESP: opcode 0x%x, seq 0x%x, len 0x%x",
		IWM_PR_ARG, __entry->opcode, __entry->seq, __entry->len
	)
);

TRACE_EVENT(iwm_rx_wifi_cmd,
	TP_PROTO(struct iwm_priv *iwm, struct iwm_umac_wifi_in_hdr *hdr),

	TP_ARGS(iwm, hdr),

	TP_STRUCT__entry(
		IWM_ENTRY
		__field(u8, cmd)
		__field(u8, source)
		__field(u16, seq)
		__field(u32, count)
	),

	TP_fast_assign(
		IWM_ASSIGN;
		__entry->cmd = hdr->sw_hdr.cmd.cmd;
		__entry->source = GET_VAL32(hdr->hw_hdr.cmd, UMAC_HDI_IN_CMD_SOURCE);
		__entry->count = GET_VAL32(hdr->sw_hdr.meta_data, UMAC_FW_CMD_BYTE_COUNT);
		__entry->seq = le16_to_cpu(hdr->sw_hdr.cmd.seq_num);
	),

	TP_printk(
		IWM_PR_FMT " Rx %s RESP: cmd 0x%x, seq 0x%x, count 0x%x",
		IWM_PR_ARG, __entry->source == UMAC_HDI_IN_SOURCE_FHRX ? "LMAC" :
		__entry->source == UMAC_HDI_IN_SOURCE_FW ? "UMAC" : "UDMA",
		__entry->cmd, __entry->seq, __entry->count
	)
);

#define iwm_ticket_action_symbol		\
	{ IWM_RX_TICKET_DROP, "DROP" },		\
	{ IWM_RX_TICKET_RELEASE, "RELEASE" },	\
	{ IWM_RX_TICKET_SNIFFER, "SNIFFER" },	\
	{ IWM_RX_TICKET_ENQUEUE, "ENQUEUE" }

TRACE_EVENT(iwm_rx_ticket,
	TP_PROTO(struct iwm_priv *iwm, void *buf, int len),

	TP_ARGS(iwm, buf, len),

	TP_STRUCT__entry(
		IWM_ENTRY
		__field(u8, action)
		__field(u8, reason)
		__field(u16, id)
		__field(u16, flags)
	),

	TP_fast_assign(
		struct iwm_rx_ticket *ticket =
			((struct iwm_umac_notif_rx_ticket *)buf)->tickets;

		IWM_ASSIGN;
		__entry->id = le16_to_cpu(ticket->id);
		__entry->action = le16_to_cpu(ticket->action);
		__entry->flags = le16_to_cpu(ticket->flags);
		__entry->reason = (__entry->flags & IWM_RX_TICKET_DROP_REASON_MSK) >> IWM_RX_TICKET_DROP_REASON_POS;
	),

	TP_printk(
		IWM_PR_FMT " Rx ticket: id 0x%x, action %s, %s 0x%x%s",
		IWM_PR_ARG, __entry->id,
		__print_symbolic(__entry->action, iwm_ticket_action_symbol),
		__entry->reason ? "reason" : "flags",
		__entry->reason ? __entry->reason : __entry->flags,
		__entry->flags & IWM_RX_TICKET_AMSDU_MSK ? ", AMSDU frame" : ""
	)
);

TRACE_EVENT(iwm_rx_packet,
	TP_PROTO(struct iwm_priv *iwm, void *buf, int len),

	TP_ARGS(iwm, buf, len),

	TP_STRUCT__entry(
		IWM_ENTRY
		__field(u8, source)
		__field(u16, id)
		__field(u32, len)
	),

	TP_fast_assign(
		struct iwm_umac_wifi_in_hdr *hdr = buf;

		IWM_ASSIGN;
		__entry->source = GET_VAL32(hdr->hw_hdr.cmd, UMAC_HDI_IN_CMD_SOURCE);
		__entry->id = le16_to_cpu(hdr->sw_hdr.cmd.seq_num);
		__entry->len = len - sizeof(*hdr);
	),

	TP_printk(
		IWM_PR_FMT " Rx %s packet: id 0x%x, %d bytes",
		IWM_PR_ARG, __entry->source == UMAC_HDI_IN_SOURCE_FHRX ?
		"LMAC" : "UMAC", __entry->id, __entry->len
	)
);
#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
