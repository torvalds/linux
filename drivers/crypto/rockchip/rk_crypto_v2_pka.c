// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co., Ltd.
 */

#include <linux/iopoll.h>

#include "rk_crypto_core.h"
#include "rk_crypto_v2.h"
#include "rk_crypto_v2_reg.h"
#include "rk_crypto_v2_pka.h"

#define PKA_WORDS2BITS(words)		((words) * 32)
#define PKA_BITS2WORDS(bits)		(((bits) + 31) / 32)

#define PKA_WORDS2BYTES(words)		((words) * 4)
#define PKA_BYTES2BITS(bytes)		((bytes) * 8)

/* PKA length set */
enum {
	PKA_EXACT_LEN_ID = 0,
	PKA_CALC_LEN_ID,
	PKA_USED_LEN_MAX,
};

/********************* Private MACRO Definition ******************************/
#define PKA_POLL_PERIOD_US	1000
#define PKA_POLL_TIMEOUT_US	50000

/* for private key EXP_MOD operation */
#define PKA_MAX_POLL_PERIOD_US	20000
#define PKA_MAX_POLL_TIMEOUT_US	2000000

#define PKA_MAX_CALC_BITS	4096
#define PKA_MAX_CALC_WORDS	PKA_BITS2WORDS(PKA_MAX_CALC_BITS)

/* PKA N_NP_T0_T1 register default (reset) value: N=0, NP=1, T0=30, T1=31 */
#define PKA_N  0UL
#define PKA_NP 1UL
#define PKA_T0 30UL		/*tmp reg */
#define PKA_T1 31UL		/*tmp reg */
#define PKA_TMP_REG_CNT		2

#define PKA_N_NP_T0_T1_REG_DEFAULT \
				(PKA_N	<< CRYPTO_N_VIRTUAL_ADDR_SHIFT	| \
				 PKA_NP << CRYPTO_NP_VIRTUAL_ADDR_SHIFT | \
				 PKA_T0 << CRYPTO_T0_VIRTUAL_ADDR_SHIFT | \
				 PKA_T1 << CRYPTO_T1_VIRTUAL_ADDR_SHIFT)

#define RES_DISCARD 0x3F

/* values for defining, that PKA entry is not in use */
#define PKA_ADDR_NOT_USED	0xFFC

/* Machine Opcodes definitions (according to HW CRS ) */

enum pka_opcode {
	PKA_OPCODE_ADD = 0x04,
	PKA_OPCODE_SUB,
	PKA_OPCODE_MOD_ADD,
	PKA_OPCODE_MOD_SUB,
	PKA_OPCODE_AND,
	PKA_OPCODE_OR,
	PKA_OPCODE_XOR,
	PKA_OPCODE_SHR0 = 0x0C,
	PKA_OPCODE_SHR1,
	PKA_OPCODE_SHL0,
	PKA_OPCODE_SHL1,
	PKA_OPCODE_LMUL,
	PKA_OPCODE_MOD_MUL,
	PKA_OPCODE_MOD_MUL_NR,
	PKA_OPCODE_MOD_EXP,
	PKA_OPCODE_DIV,
	PKA_OPCODE_MOD_INV,
	PKA_OPCODE_MOD_DIV,
	PKA_OPCODE_HMUL,
	PKA_OPCODE_TERMINATE,
};

#define PKA_CLK_ENABLE()
#define PKA_CLK_DISABLE()

#define PKA_READ(offset)	readl_relaxed((pka_base) + (offset))
#define PKA_WRITE(val, offset)	writel_relaxed((val), (pka_base) + (offset))

#define PKA_BIGNUM_WORDS(x)	(rk_bn_get_size(x) / sizeof(u32))

#define PKA_RAM_FOR_PKA()	PKA_WRITE((CRYPTO_RAM_PKA_RDY << CRYPTO_WRITE_MASK_SHIFT) | \
					  CRYPTO_RAM_PKA_RDY, CRYPTO_RAM_CTL)

#define PKA_RAM_FOR_CPU()	do { \
	PKA_WRITE((CRYPTO_RAM_PKA_RDY << CRYPTO_WRITE_MASK_SHIFT), CRYPTO_RAM_CTL); \
	while ((PKA_READ(CRYPTO_RAM_ST) & 0x01) != CRYPTO_CLK_RAM_RDY) \
		cpu_relax(); \
} while (0)

#define PKA_GET_SRAM_ADDR(addr)	((void *)(pka_base + CRYPTO_SRAM_BASE + (addr)))

/*************************************************************************
 * Macros for calling PKA operations (names according to operation issue *
 *************************************************************************/

/*--------------------------------------*/
/* 1.  ADD - SUBTRACT operations	*/
/*--------------------------------------*/
/* Add:  res =  op_a + op_b */
#define RK_PKA_ADD(op_a, op_b, res)	pka_exec_op(PKA_OPCODE_ADD, PKA_CALC_LEN_ID, \
						    0, (op_a), 0, (op_b), 0, (res), 0)

/* Clr:  res =  op_a & 0  - clears the operand A. */
#define RK_PKA_CLR(op_a)		pka_exec_op(PKA_OPCODE_AND, PKA_CALC_LEN_ID, \
						    0, (op_a), 1, 0x00, 0, (op_a), 0)

/* Copy: OpDest =  OpSrc || 0 */
#define RK_PKA_COPY(op_dest, op_src)	pka_exec_op(PKA_OPCODE_OR, PKA_CALC_LEN_ID, \
						    0, (op_src), 1, 0x00, 0, (op_dest), 0)

/* Set0: res =  op_a || 1  : set bit0 = 1, other bits are not changed */
#define RK_PKA_SET_0(op_a, res)	pka_exec_op(PKA_OPCODE_OR, PKA_CALC_LEN_ID, \
						    0, (op_a), 1, 0x01, 0, (res), 0)

/*----------------------------------------------*/
/* 3.  SHIFT  operations */
/*----------------------------------------------*/
/* SHL0: res =  op_a << (S+1) :
 * shifts left operand A by S+1 bits, insert 0 to right most bits
 */
#define RK_PKA_SHL0(op_a, S, res)	pka_exec_op(PKA_OPCODE_SHL0, PKA_CALC_LEN_ID, \
						    0, (op_a), 0, (S), 0, (res), 0)

/* SHL1:  res =  op_a << (S+1) :
 * shifts left operand A by S+1 bits, insert 1 to right most bits
 */
#define RK_PKA_SHL1(op_a, S, res)	pka_exec_op(PKA_OPCODE_SHL1, PKA_CALC_LEN_ID, \
						    0, (op_a), 0, (S), 0, (res), 0)

/*--------------------------------------------------------------*/
/*  2.  Multiplication and other operations    */
/*      Note: See notes to RK_PKAExecOperation */
/*--------------------------------------------------------------*/

/*      ModExp:  res =	op_a ** op_b  mod N - modular exponentiation */
#define RK_PKA_MOD_EXP(op_a, op_b, res)			    \
			pka_exec_op(PKA_OPCODE_MOD_EXP, PKA_EXACT_LEN_ID, 0, (op_a), \
				    0, (op_b), 0, (res), 0)

/*      Divide:  res =	op_a / op_b , op_a = op_a mod op_b - division, */
#define RK_PKA_DIV(op_a, op_b, res)	pka_exec_op(PKA_OPCODE_DIV, PKA_CALC_LEN_ID, \
						    0, (op_a), 0, (op_b), 0, (res), 0)

/*      Terminate  - special operation, which allows HOST access */
/*      to PKA data memory registers after end of PKA operations */
#define RK_PKA_TERMINATE()	pka_exec_op(PKA_OPCODE_TERMINATE, 0, 0, 0, 0, 0, 0, 0, 0)

/********************* Private Variable Definition ***************************/
static void __iomem *pka_base;

static void pka_word_memcpy(u32 *dst, u32 *src, u32 size)
{
	u32 i;

	for (i = 0; i < size; i++, dst++)
		writel_relaxed(src[i], (void *)dst);
}

static void pka_word_memset(u32 *buff, u32 val, u32 size)
{
	u32 i;

	for (i = 0; i < size; i++, buff++)
		writel_relaxed(val, (void *)buff);
}

static int pka_wait_pipe_rdy(void)
{
	u32 reg_val = 0;

	return readl_poll_timeout(pka_base + CRYPTO_PKA_PIPE_RDY, reg_val,
				  reg_val, PKA_POLL_PERIOD_US, PKA_POLL_TIMEOUT_US);
}

static int pka_wait_done(void)
{
	u32 reg_val = 0;

	return readl_poll_timeout(pka_base + CRYPTO_PKA_DONE, reg_val,
				  reg_val, PKA_POLL_PERIOD_US, PKA_POLL_TIMEOUT_US);
}

static int pka_max_wait_done(void)
{
	u32 reg_val = 0;

	return readl_poll_timeout(pka_base + CRYPTO_PKA_DONE, reg_val,
				  reg_val, PKA_MAX_POLL_PERIOD_US, PKA_MAX_POLL_TIMEOUT_US);
}

static u32 pka_check_status(u32 mask)
{
	u32 status;

	pka_wait_done();
	status = PKA_READ(CRYPTO_PKA_STATUS);
	status = status & mask;

	return !!status;
}
static void pka_set_len_words(u32 words, u32 index)
{
	PKA_WRITE(PKA_WORDS2BITS(words), CRYPTO_PKA_L0 + index * sizeof(u32));
}

static u32 pka_get_len_words(u32 index)
{
	pka_wait_done();
	return PKA_BITS2WORDS(PKA_READ(CRYPTO_PKA_L0 + (index) * sizeof(u32)));
}

static void pka_set_map_addr(u32 addr, u32 index)
{
	PKA_WRITE(addr, CRYPTO_MEMORY_MAP0 + sizeof(u32) * index);
}

static u32 pka_get_map_addr(u32 index)
{
	pka_wait_done();
	return PKA_READ(CRYPTO_MEMORY_MAP0 + sizeof(u32) * (index));
}

static u32 pka_make_full_opcode(u32 opcode, u32 len_id,
				u32 is_a_immed, u32 op_a,
				u32 is_b_immed, u32 op_b,
				u32 res_discard, u32 res,
				u32 tag)
{
	u32 full_opcode;

	full_opcode = ((opcode & 31)     << CRYPTO_OPCODE_CODE_SHIFT	|
		       (len_id & 7)      << CRYPTO_OPCODE_LEN_SHIFT	|
		       (is_a_immed & 1)  << CRYPTO_OPCODE_A_IMMED_SHIFT	|
		       (op_a & 31)       << CRYPTO_OPCODE_A_SHIFT	|
		       (is_b_immed & 1)  << CRYPTO_OPCODE_B_IMMED_SHIFT	|
		       (op_b & 31)       << CRYPTO_OPCODE_B_SHIFT	|
		       (res_discard & 1) << CRYPTO_OPCODE_R_DIS_SHIFT	|
		       (res & 31)        << CRYPTO_OPCODE_R_SHIFT	|
		       (tag & 31)        << CRYPTO_OPCODE_TAG_SHIFT);

	return full_opcode;
}

static void pka_load_data(u32 addr, u32 *data, u32 size_words)
{
	pka_wait_done();

	PKA_RAM_FOR_CPU();
	pka_word_memcpy(PKA_GET_SRAM_ADDR(addr), data, size_words);
	PKA_RAM_FOR_PKA();
}

static void pka_clr_mem(u32 addr, u32 size_words)
{
	pka_wait_done();

	PKA_RAM_FOR_CPU();
	pka_word_memset(PKA_GET_SRAM_ADDR(addr), 0x00, size_words);
	PKA_RAM_FOR_PKA();
}

static void pka_read_data(u32 addr, u32 *data, u32 size_words)
{
	pka_wait_done();

	PKA_RAM_FOR_CPU();
	pka_word_memcpy(data, PKA_GET_SRAM_ADDR(addr), size_words);
	PKA_RAM_FOR_PKA();
}

static int pka_exec_op(enum pka_opcode opcode, u8 len_id,
		       u8 is_a_immed, u8 op_a, u8 is_b_immed, u8 op_b,
		       u8 res_discard, u8 res, u8 tag)
{
	int ret = 0;
	u32 full_opcode;

	if (res == RES_DISCARD) {
		res_discard = 1;
		res = 0;
	}

	full_opcode = pka_make_full_opcode(opcode, len_id,
					   is_a_immed, op_a,
					   is_b_immed, op_b,
					   res_discard, res, tag);

	/* write full opcode into PKA CRYPTO_OPCODE register */
	PKA_WRITE(full_opcode, CRYPTO_OPCODE);

	/*************************************************/
	/* finishing operations for different cases      */
	/*************************************************/
	switch (opcode) {
	case PKA_OPCODE_DIV:
		/* for Div operation check, that op_b != 0*/
		if (pka_check_status(CRYPTO_PKA_DIV_BY_ZERO))
			goto end;
		break;
	case PKA_OPCODE_TERMINATE:
		/* wait for PKA done bit */
		ret = pka_wait_done();
		break;
	default:
		/* wait for PKA pipe ready bit */
		ret = pka_wait_pipe_rdy();
	}
end:
	return ret;
}

static int pk_int_len_tbl(u32 exact_size_words, u32 calc_size_words)
{
	u32 i;

	/* clear all length reg */
	for (i = 0; i < CRYPTO_LEN_REG_NUM; i++)
		pka_set_len_words(0, i);

	/* Case of default settings */
	/* write exact size into first table entry */
	pka_set_len_words(exact_size_words, PKA_EXACT_LEN_ID);

	/* write size with extra word into tab[1] = tab[0] + 32 */
	pka_set_len_words(calc_size_words, PKA_CALC_LEN_ID);

	return 0;
}

static int pka_int_map_tbl(u32 *regs_cnt, u32 max_size_words)
{
	u32 i;
	u32 cur_addr = 0;
	u32 max_size_bytes, default_regs_cnt;

	max_size_bytes = PKA_WORDS2BYTES(max_size_words);
	default_regs_cnt =
		min_t(u32, CRYPTO_MAP_REG_NUM, CRYPTO_SRAM_SIZE / max_size_bytes);

	/* clear all address */
	for (i = 0; i < CRYPTO_MAP_REG_NUM; i++)
		pka_set_map_addr(PKA_ADDR_NOT_USED, i);

	/* set addresses of N,NP and user requested registers (excluding 2 temp registers T0,T1) */
	for (i = 0; i < default_regs_cnt - PKA_TMP_REG_CNT; i++, cur_addr += max_size_bytes)
		pka_set_map_addr(cur_addr, i);

	/* set addresses of 2 temp registers: T0=30, T1=31 */
	pka_set_map_addr(cur_addr, PKA_T0);
	cur_addr += max_size_bytes;
	pka_set_map_addr(cur_addr, PKA_T1);

	/* output maximal count of allowed registers */
	*regs_cnt = default_regs_cnt;

	/* set default virtual addresses of N,NP,T0,T1 registers into N_NP_T0_T1_Reg */
	PKA_WRITE((u32)PKA_N_NP_T0_T1_REG_DEFAULT, CRYPTO_N_NP_T0_T1_ADDR);

	return 0;
}

static int pka_clear_regs_block(u8 first_reg, u8 regs_cnt)
{
	u32 i;
	u32 size_words;
	int cnt_tmps = 0;
	u32 user_reg_num = CRYPTO_MAP_REG_NUM - PKA_TMP_REG_CNT;

	/* calculate size_words of register in words */
	size_words = pka_get_len_words(PKA_CALC_LEN_ID);

	if (first_reg + regs_cnt > user_reg_num) {
		cnt_tmps = min_t(u8, (regs_cnt + first_reg - user_reg_num), PKA_TMP_REG_CNT);
		regs_cnt = user_reg_num;
	} else {
		cnt_tmps = PKA_TMP_REG_CNT;
	}

	/* clear ordinary registers */
	for (i = first_reg; i < regs_cnt; i++)
		RK_PKA_CLR(i);

	pka_wait_done();

	/* clear PKA temp registers (without PKA operations) */
	if (cnt_tmps > 0) {
		pka_clr_mem(pka_get_map_addr(PKA_T0), size_words);
		if (cnt_tmps > 1)
			pka_clr_mem(pka_get_map_addr(PKA_T1), size_words);

	}

	return 0;
}

static int pka_init(u32 exact_size_words)
{
	int ret;
	u32 regs_cnt = 0;
	u32 calc_size_words = exact_size_words + 1;

	PKA_CLK_ENABLE();
	PKA_RAM_FOR_PKA();

	if (exact_size_words > PKA_MAX_CALC_WORDS)
		return -1;

	ret = pk_int_len_tbl(exact_size_words, calc_size_words);
	if (ret)
		goto exit;

	ret = pka_int_map_tbl(&regs_cnt, calc_size_words);
	if (ret)
		goto exit;

	/* clean PKA data memory */
	pka_clear_regs_block(0, regs_cnt - PKA_TMP_REG_CNT);

	/* clean temp PKA registers 30,31 */
	pka_clr_mem(pka_get_map_addr(PKA_T0), calc_size_words);
	pka_clr_mem(pka_get_map_addr(PKA_T1), calc_size_words);

exit:
	return ret;
}

static void pka_finish(void)
{
	RK_PKA_TERMINATE();
	PKA_CLK_DISABLE();
}

static void pka_copy_bn_into_reg(u8 dst_reg, struct rk_bignum *bn)
{
	u32 cur_addr;
	u32 size_words, bn_words;

	RK_PKA_TERMINATE();

	bn_words = PKA_BIGNUM_WORDS(bn);
	size_words = pka_get_len_words(PKA_CALC_LEN_ID);
	cur_addr = pka_get_map_addr(dst_reg);

	pka_load_data(cur_addr, bn->data, bn_words);
	cur_addr += PKA_WORDS2BYTES(bn_words);

	pka_clr_mem(cur_addr, size_words - bn_words);
}

static int pka_copy_bn_from_reg(struct rk_bignum *bn, u32 size_words, u8 src_reg, bool is_max_poll)
{
	int ret;

	PKA_WRITE(0, CRYPTO_OPCODE);

	ret = is_max_poll ? pka_max_wait_done() : pka_wait_done();
	if (ret)
		return ret;

	pka_read_data(pka_get_map_addr(src_reg), bn->data, size_words);

	return 0;
}

/***********	pka_div_bignum function		**********************/
/**
 * @brief The function divides long number A*(2^S) by B:
 *		  res =  A*(2^S) / B,  remainder A = A*(2^S) % B.
 *	  where: A,B - are numbers of size, which is not grate than,
 *	  maximal operands size,
 *	  and B > 2^S;
 *		 S	- exponent of binary factor of A.
 *		 ^	- exponentiation operator.
 *
 *	  The function algorithm:
 *
 *	  1. Let nWords = S/32; nBits = S % 32;
 *	  2. Set res = 0, r_t1 = op_a;
 *	  3. for(i=0; i<=nWords; i++) do:
 *		  3.1. if(i < nWords )
 *				 s1 = 32;
 *			else
 *				 s1 = nBits;
 *		  3.2. r_t1 = r_t1 << s1;
 *		  3.3. call PKA_div for calculating the quotient and remainder:
 *			r_t2 = floor(r_t1/op_b) //quotient;
 *			r_t1 = r_t1 % op_b	//remainder (is in r_t1 register);
 *		  3.4. res = (res << s1) + r_t2;
 *		 end do;
 *	  4. Exit.
 *
 *	  Assuming:
 *			- 5 PKA registers are used: op_a, op_b, res, r_t1, r_t2.
 *			- The registers sizes and mapping tables are set on
 *			  default mode according to operands size.
 *			- The PKA clocks are initialized.
 *	  NOTE !   Operand op_a shall be overwritten by remainder.
 *
 * @param[in] len_id	- ID of operation size (modSize+32).
 * @param[in] op_a	- Operand A: virtual register pointer of A.
 * @param[in] S		- exponent of binary factor of A.
 * @param[in] op_b	- Operand B: virtual register pointer of B.
 * @param[in] res	- Virtual register pointer for result quotient.
 * @param[in] r_t1	- Virtual pointer to remainder.
 * @param[in] r_t2	- Virtual pointer of temp register.
 *
 * @return int - On success 0 is returned:
 *
 */
static int pka_div_bignum(u8 op_a, u32 s, u8 op_b, u8 res, u8 r_t1, u8 r_t2)
{
	u8 s1;
	u32 i;
	u32 n_bits, n_words;

	/* calculate shifting parameters (words and bits ) */
	n_words = ((u32)s + 31) / 32;
	n_bits = (u32)s % 32;

	/* copy operand op_a (including extra word) into temp reg r_t1 */
	RK_PKA_COPY(r_t1, op_a);

	/* set res = 0 (including extra word) */
	RK_PKA_CLR(res);

	/*----------------------------------------------------*/
	/* Step 1.	Shifting and dividing loop	      */
	/*----------------------------------------------------*/
	for (i = 0; i < n_words; i++) {
		/* 3.1 set shift value s1  */
		s1 = i > 0 ? 32 : n_bits;

		/* 3.2. shift: r_t1 = r_t1 * 2**s1 (in code (s1-1),
		 * because PKA performs s+1 shifts)
		 */
		if (s1 > 0)
			RK_PKA_SHL0(r_t1 /*op_a*/, (s1 - 1) /*s*/, r_t1 /*res*/);

		/* 3.3. perform PKA_OPCODE_MOD_DIV for calculating a quotient
		 * r_t2 = floor(r_t1 / N)
		 * and remainder r_t1 = r_t1 % op_b
		 */
		RK_PKA_DIV(r_t1 /*op_a*/, op_b /*B*/, r_t2 /*res*/);

		/* 3.4. res = res * 2**s1 + res;   */
		if (s1 > 0)
			RK_PKA_SHL0(res /*op_a*/, (s1 - 1) /*s*/, res /*res*/);

		RK_PKA_ADD(res /*op_a*/, r_t2 /*op_b*/, res /*res*/);
	}

	pka_wait_done();

	return 0;
}  /* END OF pka_div_bignum */

static u32 pka_calc_and_init_np(struct rk_bignum *bn, u8 r_t0, u8 r_t1, u8 r_t2)
{
	int ret;
	u32 i;
	u32 s;
	u32 mod_size_bits;
	u32 num_bits, num_words;

	/* Set s = 132 */
	s = 132;

	mod_size_bits = PKA_BYTES2BITS(rk_bn_get_size(bn));

	CRYPTO_TRACE("size_bits = %u", mod_size_bits);

	/* copy modulus N into r0 register */
	pka_copy_bn_into_reg(PKA_N, bn);

	/*--------------------------------------------------------------*/
	/* Step 1,2. Set registers: Set op_a = 2^(sizeN+32)		*/
	/*	 Registers using: 0 - N (is set in register 0,		*/
	/*	 1 - NP, temp regs: r_t0 (A), r_t1, r_t2.		*/
	/*	 len_id: 0 - exact size, 1 - exact+32 bit		*/
	/*--------------------------------------------------------------*/

	/* set register r_t0 = 0 */
	RK_PKA_CLR(r_t0);

	/* calculate bit position of said bit in the word */
	num_bits = mod_size_bits % 32;
	num_words = mod_size_bits / 32;

	CRYPTO_TRACE("num_bits = %u, num_words = %u, size_bits = %u",
		     num_bits, num_words, mod_size_bits);

	/* set 1 into register r_t0 */
	RK_PKA_SET_0(r_t0 /*op_a*/, r_t0 /*res*/);

	/* shift 1 to num_bits+31 position */
	if (num_bits > 0)
		RK_PKA_SHL0(r_t0 /*op_a*/, num_bits - 1 /*s*/, r_t0 /*res*/);

	/* shift to word position */
	for (i = 0; i < num_words; i++)
		RK_PKA_SHL0(r_t0 /*op_a*/, 31 /*s*/, r_t0 /*res*/);

	/*--------------------------------------------------------------*/
	/* Step 3.	Dividing:  PKA_NP = (r_t0 * 2**s) / N		*/
	/*--------------------------------------------------------------*/
	ret = pka_div_bignum(r_t0, s, PKA_N, PKA_NP, r_t1, r_t2);

	return ret;
}  /* END OF pka_calc_and_init_np */

/********************* Public Function Definition ****************************/

void rk_pka_set_crypto_base(void __iomem *base)
{
	pka_base = base;
}

/**
 * @brief  calculate exp mod. out = in ^ e mod n
 * @param  in: the point of input data bignum.
 * @param  e: the point of exponent bignum.
 * @param  n: the point of modulus bignum.
 * @param  out: the point of outputs bignum.
 * @param  pTmp: the point of tmpdata bignum.
 * @return 0 for success
 */
int rk_pka_expt_mod(struct rk_bignum *in,
		    struct rk_bignum *e,
		    struct rk_bignum *n,
		    struct rk_bignum *out)
{
	int ret = -1;
	u32 max_word_size;
	bool is_max_poll;
	u8 r_in = 2, r_e = 3, r_out = 4;
	u8 r_t0 = 2, r_t1 = 3, r_t2 = 4;

	if (!in || !e || !n || !out || PKA_BIGNUM_WORDS(n) == 0)
		return -1;

	max_word_size = PKA_BIGNUM_WORDS(n);

	ret = pka_init(max_word_size);
	if (ret) {
		CRYPTO_TRACE("pka_init error\n");
		goto exit;
	}

	/* calculate NP by initialization PKA for modular operations */
	ret = pka_calc_and_init_np(n, r_t0, r_t1, r_t2);
	if (ret) {
		CRYPTO_TRACE("pka_calc_and_init_np error\n");
		goto exit;
	}

	pka_clear_regs_block(r_in, 3);

	pka_copy_bn_into_reg(r_in, in);
	pka_copy_bn_into_reg(r_e, e);
	pka_copy_bn_into_reg(PKA_N, n);

	ret = RK_PKA_MOD_EXP(r_in, r_e, r_out);
	if (ret) {
		CRYPTO_TRACE("RK_PKA_MOD_EXP error\n");
		goto exit;
	}

	/* e is usually 0x10001 in public key EXP_MOD operation */
	is_max_poll = rk_bn_highest_bit(e) * 2 > rk_bn_highest_bit(n) ? true : false;

	ret = pka_copy_bn_from_reg(out, max_word_size, r_out, is_max_poll);

exit:
	pka_clear_regs_block(0, 5);
	pka_clear_regs_block(30, 2);
	pka_finish();

	return ret;
}
