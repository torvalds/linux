/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell IPSEC offload driver
 *
 * Copyright (C) 2024 Marvell.
 */

#ifndef CN10K_IPSEC_H
#define CN10K_IPSEC_H

#include <linux/types.h>

DECLARE_STATIC_KEY_FALSE(cn10k_ipsec_sa_enabled);

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

/* IPSEC Instruction opcodes */
#define CN10K_IPSEC_MAJOR_OP_WRITE_SA 0x01UL
#define CN10K_IPSEC_MINOR_OP_WRITE_SA 0x09UL
#define CN10K_IPSEC_MAJOR_OP_OUTB_IPSEC 0x2AUL

enum cn10k_cpt_comp_e {
	CN10K_CPT_COMP_E_NOTDONE = 0x00,
	CN10K_CPT_COMP_E_GOOD = 0x01,
	CN10K_CPT_COMP_E_FAULT = 0x02,
	CN10K_CPT_COMP_E_HWERR = 0x04,
	CN10K_CPT_COMP_E_INSTERR = 0x05,
	CN10K_CPT_COMP_E_WARN = 0x06,
	CN10K_CPT_COMP_E_MASK = 0x3F
};

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

	/* SA info */
	u32 sa_size;
	u32 outb_sa_count;
	struct work_struct sa_work;
	struct workqueue_struct *sa_workq;
};

/* CN10K IPSEC Security Association (SA) */
/* SA direction */
#define CN10K_IPSEC_SA_DIR_INB			0
#define CN10K_IPSEC_SA_DIR_OUTB			1
/* SA protocol */
#define CN10K_IPSEC_SA_IPSEC_PROTO_AH		0
#define CN10K_IPSEC_SA_IPSEC_PROTO_ESP		1
/* SA Encryption Type */
#define CN10K_IPSEC_SA_ENCAP_TYPE_AES_GCM	5
/* SA IPSEC mode Transport/Tunnel */
#define CN10K_IPSEC_SA_IPSEC_MODE_TRANSPORT	0
#define CN10K_IPSEC_SA_IPSEC_MODE_TUNNEL	1
/* SA AES Key Length */
#define CN10K_IPSEC_SA_AES_KEY_LEN_128		1
#define CN10K_IPSEC_SA_AES_KEY_LEN_192		2
#define CN10K_IPSEC_SA_AES_KEY_LEN_256		3
/* IV Source */
#define CN10K_IPSEC_SA_IV_SRC_COUNTER		0
#define CN10K_IPSEC_SA_IV_SRC_PACKET		3

struct cn10k_tx_sa_s {
	u64 esn_en		: 1; /* W0 */
	u64 rsvd_w0_1_8		: 8;
	u64 hw_ctx_off		: 7;
	u64 ctx_id		: 16;
	u64 rsvd_w0_32_47	: 16;
	u64 ctx_push_size	: 7;
	u64 rsvd_w0_55		: 1;
	u64 ctx_hdr_size	: 2;
	u64 aop_valid		: 1;
	u64 rsvd_w0_59		: 1;
	u64 ctx_size		: 4;
	u64 w1;			/* W1 */
	u64 sa_valid		: 1; /* W2 */
	u64 sa_dir		: 1;
	u64 rsvd_w2_2_3		: 2;
	u64 ipsec_mode		: 1;
	u64 ipsec_protocol	: 1;
	u64 aes_key_len		: 2;
	u64 enc_type		: 3;
	u64 rsvd_w2_11_19	: 9;
	u64 iv_src		: 2;
	u64 rsvd_w2_22_31	: 10;
	u64 rsvd_w2_32_63	: 32;
	u64 w3;			/* W3 */
	u8 cipher_key[32];	/* W4 - W7 */
	u32 rsvd_w8_0_31;	/* W8 : IV */
	u32 iv_gcm_salt;
	u64 rsvd_w9_w30[22];	/* W9 - W30 */
	u64 hw_ctx[6];		/* W31 - W36 */
};

/* CPT instruction parameter-1 */
#define CN10K_IPSEC_INST_PARAM1_DIS_L4_CSUM		0x1
#define CN10K_IPSEC_INST_PARAM1_DIS_L3_CSUM		0x2
#define CN10K_IPSEC_INST_PARAM1_CRYPTO_MODE		0x20
#define CN10K_IPSEC_INST_PARAM1_IV_OFFSET_SHIFT		8

/* CPT instruction parameter-2 */
#define CN10K_IPSEC_INST_PARAM2_ENC_DATA_OFFSET_SHIFT	0
#define CN10K_IPSEC_INST_PARAM2_AUTH_DATA_OFFSET_SHIFT	8

/* CPT Instruction Structure */
struct cpt_inst_s {
	u64 nixtxl		: 3; /* W0 */
	u64 doneint		: 1;
	u64 rsvd_w0_4_15	: 12;
	u64 dat_offset		: 8;
	u64 ext_param1		: 8;
	u64 nixtx_offset	: 20;
	u64 rsvd_w0_52_63	: 12;
	u64 res_addr;		/* W1 */
	u64 tag			: 32; /* W2 */
	u64 tt			: 2;
	u64 grp			: 10;
	u64 rsvd_w2_44_47	: 4;
	u64 rvu_pf_func		: 16;
	u64 qord		: 1; /* W3 */
	u64 rsvd_w3_1_2		: 2;
	u64 wqe_ptr		: 61;
	u64 dlen		: 16; /* W4 */
	u64 param2		: 16;
	u64 param1		: 16;
	u64 opcode_major	: 8;
	u64 opcode_minor	: 8;
	u64 dptr;		/* W5 */
	u64 rptr;		/* W6 */
	u64 cptr		: 60; /* W7 */
	u64 ctx_val		: 1;
	u64 egrp		: 3;
};

/* CPT Instruction Result Structure */
struct cpt_res_s {
	u64 compcode		: 7; /* W0 */
	u64 doneint		: 1;
	u64 uc_compcode		: 8;
	u64 uc_info		: 48;
	u64 esn;		/* W1 */
};

/* CPT SG structure */
struct cpt_sg_s {
	u64 seg1_size	: 16;
	u64 seg2_size	: 16;
	u64 seg3_size	: 16;
	u64 segs	: 2;
	u64 rsvd_63_50	: 14;
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

/* CPT LF CTX Flush Register */
#define CPT_LF_CTX_FLUSH GENMASK_ULL(45, 0)

#ifdef CONFIG_XFRM_OFFLOAD
int cn10k_ipsec_init(struct net_device *netdev);
void cn10k_ipsec_clean(struct otx2_nic *pf);
int cn10k_ipsec_ethtool_init(struct net_device *netdev, bool enable);
bool otx2_sqe_add_sg_ipsec(struct otx2_nic *pfvf, struct otx2_snd_queue *sq,
			   struct sk_buff *skb, int num_segs, int *offset);
bool cn10k_ipsec_transmit(struct otx2_nic *pf, struct netdev_queue *txq,
			  struct otx2_snd_queue *sq, struct sk_buff *skb,
			  int num_segs, int size);
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

static inline bool __maybe_unused
otx2_sqe_add_sg_ipsec(struct otx2_nic *pfvf, struct otx2_snd_queue *sq,
		      struct sk_buff *skb, int num_segs, int *offset)
{
	return true;
}

static inline bool __maybe_unused
cn10k_ipsec_transmit(struct otx2_nic *pf, struct netdev_queue *txq,
		     struct otx2_snd_queue *sq, struct sk_buff *skb,
		     int num_segs, int size)
{
	return true;
}
#endif
#endif // CN10K_IPSEC_H
