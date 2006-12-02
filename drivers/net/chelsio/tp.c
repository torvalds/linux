/* $Date: 2006/02/07 04:21:54 $ $RCSfile: tp.c,v $ $Revision: 1.73 $ */
#include "common.h"
#include "regs.h"
#include "tp.h"

struct petp {
	adapter_t *adapter;
};

/* Pause deadlock avoidance parameters */
#define DROP_MSEC 16
#define DROP_PKTS_CNT  1

static void tp_init(adapter_t * ap, const struct tp_params *p,
		    unsigned int tp_clk)
{
	if (t1_is_asic(ap)) {
		u32 val;

		val = F_TP_IN_CSPI_CPL | F_TP_IN_CSPI_CHECK_IP_CSUM |
		    F_TP_IN_CSPI_CHECK_TCP_CSUM | F_TP_IN_ESPI_ETHERNET;
		if (!p->pm_size)
			val |= F_OFFLOAD_DISABLE;
		else
			val |= F_TP_IN_ESPI_CHECK_IP_CSUM |
			    F_TP_IN_ESPI_CHECK_TCP_CSUM;
		writel(val, ap->regs + A_TP_IN_CONFIG);
		writel(F_TP_OUT_CSPI_CPL |
		       F_TP_OUT_ESPI_ETHERNET |
		       F_TP_OUT_ESPI_GENERATE_IP_CSUM |
		       F_TP_OUT_ESPI_GENERATE_TCP_CSUM,
		       ap->regs + A_TP_OUT_CONFIG);
		writel(V_IP_TTL(64) |
		       F_PATH_MTU /* IP DF bit */  |
		       V_5TUPLE_LOOKUP(p->use_5tuple_mode) |
		       V_SYN_COOKIE_PARAMETER(29),
		       ap->regs + A_TP_GLOBAL_CONFIG);
		/*
		 * Enable pause frame deadlock prevention.
		 */
		if (is_T2(ap) && ap->params.nports > 1) {
			u32 drop_ticks = DROP_MSEC * (tp_clk / 1000);

			writel(F_ENABLE_TX_DROP | F_ENABLE_TX_ERROR |
			       V_DROP_TICKS_CNT(drop_ticks) |
			       V_NUM_PKTS_DROPPED(DROP_PKTS_CNT),
			       ap->regs + A_TP_TX_DROP_CONFIG);
		}

	}
}

void t1_tp_destroy(struct petp *tp)
{
	kfree(tp);
}

struct petp *__devinit t1_tp_create(adapter_t * adapter, struct tp_params *p)
{
	struct petp *tp = kzalloc(sizeof(*tp), GFP_KERNEL);
	if (!tp)
		return NULL;

	tp->adapter = adapter;

	return tp;
}

void t1_tp_intr_enable(struct petp *tp)
{
	u32 tp_intr = readl(tp->adapter->regs + A_PL_ENABLE);

	{
		/* We don't use any TP interrupts */
		writel(0, tp->adapter->regs + A_TP_INT_ENABLE);
		writel(tp_intr | F_PL_INTR_TP,
		       tp->adapter->regs + A_PL_ENABLE);
	}
}

void t1_tp_intr_disable(struct petp *tp)
{
	u32 tp_intr = readl(tp->adapter->regs + A_PL_ENABLE);

	{
		writel(0, tp->adapter->regs + A_TP_INT_ENABLE);
		writel(tp_intr & ~F_PL_INTR_TP,
		       tp->adapter->regs + A_PL_ENABLE);
	}
}

void t1_tp_intr_clear(struct petp *tp)
{
	writel(0xffffffff, tp->adapter->regs + A_TP_INT_CAUSE);
	writel(F_PL_INTR_TP, tp->adapter->regs + A_PL_CAUSE);
}

int t1_tp_intr_handler(struct petp *tp)
{
	u32 cause;


	cause = readl(tp->adapter->regs + A_TP_INT_CAUSE);
	writel(cause, tp->adapter->regs + A_TP_INT_CAUSE);
	return 0;
}

static void set_csum_offload(struct petp *tp, u32 csum_bit, int enable)
{
	u32 val = readl(tp->adapter->regs + A_TP_GLOBAL_CONFIG);

	if (enable)
		val |= csum_bit;
	else
		val &= ~csum_bit;
	writel(val, tp->adapter->regs + A_TP_GLOBAL_CONFIG);
}

void t1_tp_set_ip_checksum_offload(struct petp *tp, int enable)
{
	set_csum_offload(tp, F_IP_CSUM, enable);
}

void t1_tp_set_udp_checksum_offload(struct petp *tp, int enable)
{
	set_csum_offload(tp, F_UDP_CSUM, enable);
}

void t1_tp_set_tcp_checksum_offload(struct petp *tp, int enable)
{
	set_csum_offload(tp, F_TCP_CSUM, enable);
}

/*
 * Initialize TP state.  tp_params contains initial settings for some TP
 * parameters, particularly the one-time PM and CM settings.
 */
int t1_tp_reset(struct petp *tp, struct tp_params *p, unsigned int tp_clk)
{
	adapter_t *adapter = tp->adapter;

	tp_init(adapter, p, tp_clk);
	writel(F_TP_RESET, adapter->regs +  A_TP_RESET);
	return 0;
}
