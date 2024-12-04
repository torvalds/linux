/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell IPSEC offload driver
 *
 * Copyright (C) 2024 Marvell.
 */

#ifndef CN10K_IPSEC_H
#define CN10K_IPSEC_H

#include <linux/types.h>

/* CPT instruction size in bytes */
#define CN10K_CPT_INST_SIZE	64

/* CPT instruction (CPT_INST_S) queue length */
#define CN10K_CPT_INST_QLEN	8200

/* CPT instruction queue size passed to HW is in units of
 * 40*CPT_INST_S messages.
 */
#define CN10K_CPT_SIZE_DIV40 (CN10K_CPT_INST_QLEN / 40)

/* CPT needs 320 free entries */
#define CN10K_CPT_INST_QLEN_EXTRA_BYTES	(320 * CN10K_CPT_INST_SIZE)
#define CN10K_CPT_EXTRA_SIZE_DIV40	(320 / 40)

/* CPT instruction queue length in bytes */
#define CN10K_CPT_INST_QLEN_BYTES					\
		((CN10K_CPT_SIZE_DIV40 * 40 * CN10K_CPT_INST_SIZE) +	\
		CN10K_CPT_INST_QLEN_EXTRA_BYTES)

/* CPT instruction group queue length in bytes */
#define CN10K_CPT_INST_GRP_QLEN_BYTES					\
		((CN10K_CPT_SIZE_DIV40 + CN10K_CPT_EXTRA_SIZE_DIV40) * 16)

/* CPT FC length in bytes */
#define CN10K_CPT_Q_FC_LEN 128

/* Default CPT engine group for ipsec offload */
#define CN10K_DEF_CPT_IPSEC_EGRP 1

/* CN10K CPT LF registers */
#define CPT_LFBASE			(BLKTYPE_CPT << RVU_FUNC_BLKADDR_SHIFT)
#define CN10K_CPT_LF_CTL		(CPT_LFBASE | 0x10)
#define CN10K_CPT_LF_INPROG		(CPT_LFBASE | 0x40)
#define CN10K_CPT_LF_Q_BASE		(CPT_LFBASE | 0xf0)
#define CN10K_CPT_LF_Q_SIZE		(CPT_LFBASE | 0x100)
#define CN10K_CPT_LF_Q_INST_PTR		(CPT_LFBASE | 0x110)
#define CN10K_CPT_LF_Q_GRP_PTR		(CPT_LFBASE | 0x120)
#define CN10K_CPT_LF_NQX(a)		(CPT_LFBASE | 0x400 | (a) << 3)
#define CN10K_CPT_LF_CTX_FLUSH		(CPT_LFBASE | 0x510)

struct cn10k_cpt_inst_queue {
	u8 *vaddr;
	u8 *real_vaddr;
	dma_addr_t dma_addr;
	dma_addr_t real_dma_addr;
	u32 size;
};

enum cn10k_cpt_hw_state_e {
	CN10K_CPT_HW_UNAVAILABLE,
	CN10K_CPT_HW_AVAILABLE,
	CN10K_CPT_HW_IN_USE
};

struct cn10k_ipsec {
	/* Outbound CPT */
	u64 io_addr;
	atomic_t cpt_state;
	struct cn10k_cpt_inst_queue iq;
};

/* CPT LF_INPROG Register */
#define CPT_LF_INPROG_INFLIGHT	GENMASK_ULL(8, 0)
#define CPT_LF_INPROG_GRB_CNT	GENMASK_ULL(39, 32)
#define CPT_LF_INPROG_GWB_CNT	GENMASK_ULL(47, 40)

/* CPT LF_Q_GRP_PTR Register */
#define CPT_LF_Q_GRP_PTR_DQ_PTR	GENMASK_ULL(14, 0)
#define CPT_LF_Q_GRP_PTR_NQ_PTR	GENMASK_ULL(46, 32)

/* CPT LF_Q_SIZE Register */
#define CPT_LF_Q_BASE_ADDR GENMASK_ULL(52, 7)

/* CPT LF_Q_SIZE Register */
#define CPT_LF_Q_SIZE_DIV40 GENMASK_ULL(14, 0)

#ifdef CONFIG_XFRM_OFFLOAD
int cn10k_ipsec_init(struct net_device *netdev);
void cn10k_ipsec_clean(struct otx2_nic *pf);
int cn10k_ipsec_ethtool_init(struct net_device *netdev, bool enable);
#else
static inline __maybe_unused int cn10k_ipsec_init(struct net_device *netdev)
{
	return 0;
}

static inline __maybe_unused void cn10k_ipsec_clean(struct otx2_nic *pf)
{
}

static inline __maybe_unused
int cn10k_ipsec_ethtool_init(struct net_device *netdev, bool enable)
{
	return 0;
}
#endif
#endif // CN10K_IPSEC_H
