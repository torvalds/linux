// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include "bnx2x.h"

#define NA 0xCD

#define IDLE_CHK_E1			0x01
#define IDLE_CHK_E1H			0x02
#define IDLE_CHK_E2			0x04
#define IDLE_CHK_E3A0			0x08
#define IDLE_CHK_E3B0			0x10

#define IDLE_CHK_ERROR			1
#define IDLE_CHK_ERROR_NO_TRAFFIC	2
#define IDLE_CHK_WARNING		3

#define MAX_FAIL_MSG 256

/* statistics and error reporting */
static int idle_chk_errors, idle_chk_warnings;

/* masks for all chip types */
static int is_e1, is_e1h, is_e2, is_e3a0, is_e3b0;

/* struct for the argument list for a predicate in the self test databasei */
struct st_pred_args {
	u32 val1; /* value read from first register */
	u32 val2; /* value read from second register, if applicable */
	u32 imm1; /* 1st value in predicate condition, left-to-right */
	u32 imm2; /* 2nd value in predicate condition, left-to-right */
	u32 imm3; /* 3rd value in predicate condition, left-to-right */
	u32 imm4; /* 4th value in predicate condition, left-to-right */
};

/* struct representing self test record - a single test */
struct st_record {
	u8 chip_mask;
	u8 macro;
	u32 reg1;
	u32 reg2;
	u16 loop;
	u16 incr;
	int (*bnx2x_predicate)(struct st_pred_args *pred_args);
	u32 reg3;
	u8 severity;
	char *fail_msg;
	struct st_pred_args pred_args;
};

/* predicates for self test */
static int peq(struct st_pred_args *args)
{
	return (args->val1 == args->imm1);
}

static int pneq(struct st_pred_args *args)
{
	return (args->val1 != args->imm1);
}

static int pand_neq(struct st_pred_args *args)
{
	return ((args->val1 & args->imm1) != args->imm2);
}

static int pand_neq_x2(struct st_pred_args *args)
{
	return (((args->val1 & args->imm1) != args->imm2) &&
		((args->val1 & args->imm3) != args->imm4));
}

static int pneq_err(struct st_pred_args *args)
{
	return ((args->val1 != args->imm1) && (idle_chk_errors > args->imm2));
}

static int pgt(struct st_pred_args *args)
{
	return (args->val1 > args->imm1);
}

static int pneq_r2(struct st_pred_args *args)
{
	return (args->val1 != args->val2);
}

static int plt_sub_r2(struct st_pred_args *args)
{
	return (args->val1 < (args->val2 - args->imm1));
}

static int pne_sub_r2(struct st_pred_args *args)
{
	return (args->val1 != (args->val2 - args->imm1));
}

static int prsh_and_neq(struct st_pred_args *args)
{
	return (((args->val1 >> args->imm1) & args->imm2) != args->imm3);
}

static int peq_neq_r2(struct st_pred_args *args)
{
	return ((args->val1 == args->imm1) && (args->val2 != args->imm2));
}

static int peq_neq_neq_r2(struct st_pred_args *args)
{
	return ((args->val1 == args->imm1) && (args->val2 != args->imm2) &&
		(args->val2 != args->imm3));
}

/* struct holding the database of self test checks (registers and predicates) */
/* lines start from 2 since line 1 is heading in csv */
#define ST_DB_LINES 468
static struct st_record st_database[ST_DB_LINES] = {
/*line 2*/{(0x3), 1, 0x2114,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"PCIE: ucorr_err_status is not 0",
	{NA, NA, 0x0FF010, 0, NA, NA} },

/*line 3*/{(0x3), 1, 0x2114,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"PCIE: ucorr_err_status - Unsupported request error",
	{NA, NA, 0x100000, 0, NA, NA} },

/*line 4*/{(0x3), 1, 0x2120,
	NA, 1, 0, pand_neq_x2,
	NA, IDLE_CHK_WARNING,
	"PCIE: corr_err_status is not 0x2000",
	{NA, NA, 0x31C1, 0x2000, 0x31C1, 0} },

/*line 5*/{(0x3), 1, 0x2814,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"PCIE: attentions register is not 0x40100",
	{NA, NA, ~0x40100, 0, NA, NA} },

/*line 6*/{(0x2), 1, 0x281c,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"PCIE: attentions register is not 0x40040100",
	{NA, NA, ~0x40040100, 0, NA, NA} },

/*line 7*/{(0x2), 1, 0x2820,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"PCIE: attentions register is not 0x40040100",
	{NA, NA, ~0x40040100, 0, NA, NA} },

/*line 8*/{(0x3), 1, PXP2_REG_PGL_EXP_ROM2,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP2: There are outstanding read requests. Not all completios have arrived for read requests on tags that are marked with 0",
	{NA, NA, 0xffffffff, NA, NA, NA} },

/*line 9*/{(0x3), 2, 0x212c,
	NA, 4, 4, pneq_err,
	NA, IDLE_CHK_WARNING,
	"PCIE: error packet header is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 10*/{(0x1C), 1, 0x2104,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"PCIE: ucorr_err_status is not 0",
	{NA, NA, 0x0FD010, 0, NA, NA} },

/*line 11*/{(0x1C), 1, 0x2104,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"PCIE: ucorr_err_status - Unsupported request error",
	{NA, NA, 0x100000, 0, NA, NA} },

/*line 12*/{(0x1C), 1, 0x2104,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"PCIE: ucorr_err_status - Flow Control Protocol Error",
	{NA, NA, 0x2000, 0, NA, NA} },

/*line 13*/{(0x1C), 1, 0x2110,
	NA, 1, 0, pand_neq_x2,
	NA, IDLE_CHK_WARNING,
	"PCIE: corr_err_status is not 0x2000",
	{NA, NA, 0x31C1, 0x2000, 0x31C1, 0} },

/*line 14*/{(0x1C), 1, 0x2814,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"PCIE: TTX_BRIDGE_FORWARD_ERR - Received master request while BME was 0",
	{NA, NA, 0x2000000, 0, NA, NA} },

/*line 15*/{(0x1C), 1, 0x2814,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"PCIE: Func 0 1: attentions register is not 0x2040902",
	{NA, NA, ~0x2040902, 0, NA, NA} },

/*line 16*/{(0x1C), 1, 0x2854,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"PCIE: Func 2 3 4: attentions register is not 0x10240902",
	{NA, NA, ~0x10240902, 0, NA, NA} },

/*line 17*/{(0x1C), 1, 0x285c,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"PCIE: Func 5 6 7: attentions register is not 0x10240902",
	{NA, NA, ~0x10240902, 0, NA, NA} },

/*line 18*/{(0x18), 1, 0x3040,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"PCIE: Overflow in DLP2TLP buffer",
	{NA, NA, 0x2, 0, NA, NA} },

/*line 19*/{(0x1C), 1, PXP2_REG_PGL_EXP_ROM2,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP2: There are outstanding read requests for tags 0-31. Not all completios have arrived for read requests on tags that are marked with 0",
	{NA, NA, 0xffffffff, NA, NA, NA} },

/*line 20*/{(0x1C), 2, 0x211c,
	NA, 4, 4, pneq_err,
	NA, IDLE_CHK_WARNING,
	"PCIE: error packet header is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 21*/{(0x1C), 1, PGLUE_B_REG_INCORRECT_RCV_DETAILS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"PGLUE_B: Packet received from PCIe not according to the rules",
	{NA, NA, 0, NA, NA, NA} },

/*line 22*/{(0x1C), 1, PGLUE_B_REG_WAS_ERROR_VF_31_0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: was_error for VFs 0-31 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 23*/{(0x1C), 1, PGLUE_B_REG_WAS_ERROR_VF_63_32,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: was_error for VFs 32-63 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 24*/{(0x1C), 1, PGLUE_B_REG_WAS_ERROR_VF_95_64,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: was_error for VFs 64-95 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 25*/{(0x1C), 1, PGLUE_B_REG_WAS_ERROR_VF_127_96,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: was_error for VFs 96-127 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 26*/{(0x1C), 1, PGLUE_B_REG_WAS_ERROR_PF_7_0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: was_error for PFs 0-7 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 27*/{(0x1C), 1, PGLUE_B_REG_RX_ERR_DETAILS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: Completion received with error. (2:0) - PFID. (3) - VF_VALID. (9:4) - VFID. (11:10) - Error code : 0 - Completion Timeout; 1 - Unsupported Request; 2 - Completer Abort. (12) - valid bit",
	{NA, NA, 0, NA, NA, NA} },

/*line 28*/{(0x1C), 1, PGLUE_B_REG_RX_TCPL_ERR_DETAILS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: ATS TCPL received with error. (2:0) - PFID. (3) - VF_VALID. (9:4) - VFID. (11:10) - Error code : 0 - Completion Timeout ; 1 - Unsupported Request; 2 - Completer Abort. (16:12) - OTB Entry ID. (17) - valid bit",
	{NA, NA, 0, NA, NA, NA} },

/*line 29*/{(0x1C), 1, PGLUE_B_REG_TX_ERR_WR_ADD_31_0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: Error in master write. Address(31:0) is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 30*/{(0x1C), 1, PGLUE_B_REG_TX_ERR_WR_ADD_63_32,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: Error in master write. Address(63:32) is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 31*/{(0x1C), 1, PGLUE_B_REG_TX_ERR_WR_DETAILS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: Error in master write. Error details register is not 0. (4:0) VQID. (23:21) - PFID. (24) - VF_VALID. (30:25) - VFID",
	{NA, NA, 0, NA, NA, NA} },

/*line 32*/{(0x1C), 1, PGLUE_B_REG_TX_ERR_WR_DETAILS2,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: Error in master write. Error details 2nd register is not 0. (21) - was_error set; (22) - BME cleared; (23) - FID_enable cleared; (24) - VF with parent PF FLR_request or IOV_disable_request",
	{NA, NA, 0, NA, NA, NA} },

/*line 33*/{(0x1C), 1, PGLUE_B_REG_TX_ERR_RD_ADD_31_0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE: Error in master read address(31:0) is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 34*/{(0x1C), 1, PGLUE_B_REG_TX_ERR_RD_ADD_63_32,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: Error in master read address(63:32) is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 35*/{(0x1C), 1, PGLUE_B_REG_TX_ERR_RD_DETAILS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: Error in master read Error details register is not 0. (4:0) VQID. (23:21) - PFID. (24) - VF_VALID. (30:25) - VFID",
	{NA, NA, 0, NA, NA, NA} },

/*line 36*/{(0x1C), 1, PGLUE_B_REG_TX_ERR_RD_DETAILS2,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: Error in master read Error details 2nd register is not 0. (21) - was_error set; (22) - BME cleared; (23) - FID_enable cleared; (24) - VF with parent PF FLR_request or IOV_disable_request",
	{NA, NA, 0, NA, NA, NA} },

/*line 37*/{(0x1C), 1, PGLUE_B_REG_VF_LENGTH_VIOLATION_DETAILS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: Target VF length violation access",
	{NA, NA, 0, NA, NA, NA} },

/*line 38*/{(0x1C), 1, PGLUE_B_REG_VF_GRC_SPACE_VIOLATION_DETAILS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: Target VF GRC space access failed permission check",
	{NA, NA, 0, NA, NA, NA} },

/*line 39*/{(0x1C), 1, PGLUE_B_REG_TAGS_63_32,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: There are outstanding read requests for tags 32-63. Not all completios have arrived for read requests on tags that are marked with 0",
	{NA, NA, 0xffffffff, NA, NA, NA} },

/*line 40*/{(0x1C), 3, PXP_REG_HST_VF_DISABLED_ERROR_VALID,
	PXP_REG_HST_VF_DISABLED_ERROR_DATA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP: Access to disabled VF took place",
	{NA, NA, 0, NA, NA, NA} },

/*line 41*/{(0x1C), 1, PXP_REG_HST_PER_VIOLATION_VALID,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP: Zone A permission violation occurred",
	{NA, NA, 0, NA, NA, NA} },

/*line 42*/{(0x1C), 1, PXP_REG_HST_INCORRECT_ACCESS_VALID,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP: Incorrect transaction took place",
	{NA, NA, 0, NA, NA, NA} },

/*line 43*/{(0x1C), 1, PXP2_REG_RD_CPL_ERR_DETAILS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP2: Completion received with error. Error details register is not 0. (15:0) - ECHO. (28:16) - Sub Request length plus start_offset_2_0 minus 1",
	{NA, NA, 0, NA, NA, NA} },

/*line 44*/{(0x1C), 1, PXP2_REG_RD_CPL_ERR_DETAILS2,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP2: Completion received with error. Error details 2nd register is not 0. (4:0) - VQ ID. (8:5) - client ID. (9) - valid bit",
	{NA, NA, 0, NA, NA, NA} },

/*line 45*/{(0x1F), 1, PXP2_REG_RQ_VQ0_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ0 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 46*/{(0x1F), 1, PXP2_REG_RQ_VQ1_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ1 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 47*/{(0x1F), 1, PXP2_REG_RQ_VQ2_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ2 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 48*/{(0x1F), 1, PXP2_REG_RQ_VQ3_ENTRY_CNT,
	NA, 1, 0, pgt,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ3 is not empty",
	{NA, NA, 2, NA, NA, NA} },

/*line 49*/{(0x1F), 1, PXP2_REG_RQ_VQ4_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ4 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 50*/{(0x1F), 1, PXP2_REG_RQ_VQ5_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ5 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 51*/{(0x1F), 1, PXP2_REG_RQ_VQ6_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ6 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 52*/{(0x1F), 1, PXP2_REG_RQ_VQ7_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ7 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 53*/{(0x1F), 1, PXP2_REG_RQ_VQ8_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ8 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 54*/{(0x1F), 1, PXP2_REG_RQ_VQ9_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ9 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 55*/{(0x1F), 1, PXP2_REG_RQ_VQ10_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ10 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 56*/{(0x1F), 1, PXP2_REG_RQ_VQ11_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ11 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 57*/{(0x1F), 1, PXP2_REG_RQ_VQ12_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ12 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 58*/{(0x1F), 1, PXP2_REG_RQ_VQ13_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ13 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 59*/{(0x1F), 1, PXP2_REG_RQ_VQ14_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ14 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 60*/{(0x1F), 1, PXP2_REG_RQ_VQ15_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ15 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 61*/{(0x1F), 1, PXP2_REG_RQ_VQ16_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ16 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 62*/{(0x1F), 1, PXP2_REG_RQ_VQ17_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ17 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 63*/{(0x1F), 1, PXP2_REG_RQ_VQ18_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ18 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 64*/{(0x1F), 1, PXP2_REG_RQ_VQ19_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ19 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 65*/{(0x1F), 1, PXP2_REG_RQ_VQ20_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ20 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 66*/{(0x1F), 1, PXP2_REG_RQ_VQ21_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ21 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 67*/{(0x1F), 1, PXP2_REG_RQ_VQ22_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ22 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 68*/{(0x1F), 1, PXP2_REG_RQ_VQ23_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ23 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 69*/{(0x1F), 1, PXP2_REG_RQ_VQ24_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ24 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 70*/{(0x1F), 1, PXP2_REG_RQ_VQ25_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ25 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 71*/{(0x1F), 1, PXP2_REG_RQ_VQ26_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ26 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 72*/{(0x1F), 1, PXP2_REG_RQ_VQ27_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ27 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 73*/{(0x1F), 1, PXP2_REG_RQ_VQ28_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ28 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 74*/{(0x1F), 1, PXP2_REG_RQ_VQ29_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ29 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 75*/{(0x1F), 1, PXP2_REG_RQ_VQ30_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ30 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 76*/{(0x1F), 1, PXP2_REG_RQ_VQ31_ENTRY_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: VQ31 is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 77*/{(0x1F), 1, PXP2_REG_RQ_UFIFO_NUM_OF_ENTRY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: rq_ufifo_num_of_entry is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 78*/{(0x1F), 1, PXP2_REG_RQ_RBC_DONE,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"PXP2: rq_rbc_done is not 1",
	{NA, NA, 1, NA, NA, NA} },

/*line 79*/{(0x1F), 1, PXP2_REG_RQ_CFG_DONE,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"PXP2: rq_cfg_done is not 1",
	{NA, NA, 1, NA, NA, NA} },

/*line 80*/{(0x3), 1, PXP2_REG_PSWRQ_BW_CREDIT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: rq_read_credit and rq_write_credit are not 3",
	{NA, NA, 0x1B, NA, NA, NA} },

/*line 81*/{(0x1F), 1, PXP2_REG_RD_START_INIT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"PXP2: rd_start_init is not 1",
	{NA, NA, 1, NA, NA, NA} },

/*line 82*/{(0x1F), 1, PXP2_REG_RD_INIT_DONE,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"PXP2: rd_init_done is not 1",
	{NA, NA, 1, NA, NA, NA} },

/*line 83*/{(0x1F), 3, PXP2_REG_RD_SR_CNT,
	PXP2_REG_RD_SR_NUM_CFG, 1, 0, pne_sub_r2,
	NA, IDLE_CHK_WARNING,
	"PXP2: rd_sr_cnt is not equal to rd_sr_num_cfg",
	{NA, NA, 1, NA, NA, NA} },

/*line 84*/{(0x1F), 3, PXP2_REG_RD_BLK_CNT,
	PXP2_REG_RD_BLK_NUM_CFG, 1, 0, pneq_r2,
	NA, IDLE_CHK_WARNING,
	"PXP2: rd_blk_cnt is not equal to rd_blk_num_cfg",
	{NA, NA, NA, NA, NA, NA} },

/*line 85*/{(0x1F), 3, PXP2_REG_RD_SR_CNT,
	PXP2_REG_RD_SR_NUM_CFG, 1, 0, plt_sub_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: There are more than two unused SRs",
	{NA, NA, 3, NA, NA, NA} },

/*line 86*/{(0x1F), 3, PXP2_REG_RD_BLK_CNT,
	PXP2_REG_RD_BLK_NUM_CFG, 1, 0, plt_sub_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: There are more than two unused blocks",
	{NA, NA, 2, NA, NA, NA} },

/*line 87*/{(0x1F), 1, PXP2_REG_RD_PORT_IS_IDLE_0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: P0 All delivery ports are not idle",
	{NA, NA, 1, NA, NA, NA} },

/*line 88*/{(0x1F), 1, PXP2_REG_RD_PORT_IS_IDLE_1,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: P1 All delivery ports are not idle",
	{NA, NA, 1, NA, NA, NA} },

/*line 89*/{(0x1F), 2, PXP2_REG_RD_ALMOST_FULL_0,
	NA, 11, 4, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: rd_almost_full is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 90*/{(0x1F), 1, PXP2_REG_RD_DISABLE_INPUTS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"PXP2: PSWRD inputs are disabled",
	{NA, NA, 0, NA, NA, NA} },

/*line 91*/{(0x1F), 1, PXP2_REG_HST_HEADER_FIFO_STATUS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: HST header FIFO status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 92*/{(0x1F), 1, PXP2_REG_HST_DATA_FIFO_STATUS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: HST data FIFO status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 93*/{(0x3), 1, PXP2_REG_PGL_WRITE_BLOCKED,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"PXP2: pgl_write_blocked is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 94*/{(0x3), 1, PXP2_REG_PGL_READ_BLOCKED,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"PXP2: pgl_read_blocked is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 95*/{(0x1C), 1, PXP2_REG_PGL_WRITE_BLOCKED,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP2: pgl_write_blocked is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 96*/{(0x1C), 1, PXP2_REG_PGL_READ_BLOCKED,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP2: pgl_read_blocked is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 97*/{(0x1F), 1, PXP2_REG_PGL_TXW_CDTS,
	NA, 1, 0, prsh_and_neq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PXP2: There is data which is ready",
	{NA, NA, 17, 1, 0, NA} },

/*line 98*/{(0x1F), 1, PXP_REG_HST_ARB_IS_IDLE,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP: HST arbiter is not idle",
	{NA, NA, 1, NA, NA, NA} },

/*line 99*/{(0x1F), 1, PXP_REG_HST_CLIENTS_WAITING_TO_ARB,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP: HST one of the clients is waiting for delivery",
	{NA, NA, 0, NA, NA, NA} },

/*line 100*/{(0x1E), 1, PXP_REG_HST_DISCARD_INTERNAL_WRITES_STATUS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP: HST Close the gates: Discarding internal writes",
	{NA, NA, 0, NA, NA, NA} },

/*line 101*/{(0x1E), 1, PXP_REG_HST_DISCARD_DOORBELLS_STATUS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP: HST Close the gates: Discarding doorbells",
	{NA, NA, 0, NA, NA, NA} },

/*line 102*/{(0x1C), 1, PXP2_REG_RQ_GARB,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"PXP2: PSWRQ Close the gates is asserted. Check AEU AFTER_INVERT registers for parity errors",
	{NA, NA, 0x1000, 0, NA, NA} },

/*line 103*/{(0x1F), 1, DMAE_REG_GO_C0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DMAE: command 0 go is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 104*/{(0x1F), 1, DMAE_REG_GO_C1,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DMAE: command 1 go is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 105*/{(0x1F), 1, DMAE_REG_GO_C2,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DMAE: command 2 go is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 106*/{(0x1F), 1, DMAE_REG_GO_C3,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DMAE: command 3 go is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 107*/{(0x1F), 1, DMAE_REG_GO_C4,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DMAE: command 4 go is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 108*/{(0x1F), 1, DMAE_REG_GO_C5,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DMAE: command 5 go is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 109*/{(0x1F), 1, DMAE_REG_GO_C6,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DMAE: command 6 go is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 110*/{(0x1F), 1, DMAE_REG_GO_C7,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DMAE: command 7 go is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 111*/{(0x1F), 1, DMAE_REG_GO_C8,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DMAE: command 8 go is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 112*/{(0x1F), 1, DMAE_REG_GO_C9,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DMAE: command 9 go is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 113*/{(0x1F), 1, DMAE_REG_GO_C10,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DMAE: command 10 go is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 114*/{(0x1F), 1, DMAE_REG_GO_C11,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DMAE: command 11 go is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 115*/{(0x1F), 1, DMAE_REG_GO_C12,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DMAE: command 12 go is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 116*/{(0x1F), 1, DMAE_REG_GO_C13,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DMAE: command 13 go is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 117*/{(0x1F), 1, DMAE_REG_GO_C14,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DMAE: command 14 go is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 118*/{(0x1F), 1, DMAE_REG_GO_C15,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DMAE: command 15 go is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 119*/{(0x1F), 1, CFC_REG_ERROR_VECTOR,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"CFC: error vector is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 120*/{(0x1F), 1, CFC_REG_NUM_LCIDS_ARRIVING,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"CFC: number of arriving LCIDs is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 121*/{(0x1F), 1, CFC_REG_NUM_LCIDS_ALLOC,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"CFC: number of alloc LCIDs is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 122*/{(0x1F), 1, CFC_REG_NUM_LCIDS_LEAVING,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"CFC: number of leaving LCIDs is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 123*/{(0x1F), 7, CFC_REG_INFO_RAM,
	CFC_REG_CID_CAM, (CFC_REG_INFO_RAM_SIZE >> 4), 16, peq_neq_neq_r2,
	CFC_REG_ACTIVITY_COUNTER, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CFC: AC is neither 0 nor 2 on connType 0 (ETH)",
	{NA, NA, 0, 0, 2, NA} },

/*line 124*/{(0x1F), 7, CFC_REG_INFO_RAM,
	CFC_REG_CID_CAM, (CFC_REG_INFO_RAM_SIZE >> 4), 16, peq_neq_r2,
	CFC_REG_ACTIVITY_COUNTER, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CFC: AC is not 0 on connType 1 (TOE)",
	{NA, NA, 1, 0, NA, NA} },

/*line 125*/{(0x1F), 7, CFC_REG_INFO_RAM,
	CFC_REG_CID_CAM, (CFC_REG_INFO_RAM_SIZE >> 4), 16, peq_neq_r2,
	CFC_REG_ACTIVITY_COUNTER, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CFC: AC is not 0 on connType 3 (iSCSI)",
	{NA, NA, 3, 0, NA, NA} },

/*line 126*/{(0x1F), 7, CFC_REG_INFO_RAM,
	CFC_REG_CID_CAM, (CFC_REG_INFO_RAM_SIZE >> 4), 16, peq_neq_r2,
	CFC_REG_ACTIVITY_COUNTER, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CFC: AC is not 0 on connType 4 (FCoE)",
	{NA, NA, 4, 0, NA, NA} },

/*line 127*/{(0x1F), 2, QM_REG_QTASKCTR_0,
	NA, 64, 4, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: Queue is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 128*/{(0xF), 3, QM_REG_VOQCREDIT_0,
	QM_REG_VOQINITCREDIT_0, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: VOQ_0, VOQ credit is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 129*/{(0xF), 3, QM_REG_VOQCREDIT_1,
	QM_REG_VOQINITCREDIT_1, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: VOQ_1, VOQ credit is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 130*/{(0xF), 3, QM_REG_VOQCREDIT_4,
	QM_REG_VOQINITCREDIT_4, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: VOQ_4, VOQ credit is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 131*/{(0x3), 3, QM_REG_PORT0BYTECRD,
	QM_REG_BYTECRDINITVAL, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: P0 Byte credit is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 132*/{(0x3), 3, QM_REG_PORT1BYTECRD,
	QM_REG_BYTECRDINITVAL, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: P1 Byte credit is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 133*/{(0x1F), 1, CCM_REG_CAM_OCCUP,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CCM: XX protection CAM is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 134*/{(0x1F), 1, TCM_REG_CAM_OCCUP,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"TCM: XX protection CAM is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 135*/{(0x1F), 1, UCM_REG_CAM_OCCUP,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"UCM: XX protection CAM is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 136*/{(0x1F), 1, XCM_REG_CAM_OCCUP,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"XCM: XX protection CAM is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 137*/{(0x1F), 1, BRB1_REG_NUM_OF_FULL_BLOCKS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"BRB1: BRB is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 138*/{(0x1F), 1, CSEM_REG_SLEEP_THREADS_VALID,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CSEM: There are sleeping threads",
	{NA, NA, 0, NA, NA, NA} },

/*line 139*/{(0x1F), 1, TSEM_REG_SLEEP_THREADS_VALID,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"TSEM: There are sleeping threads",
	{NA, NA, 0, NA, NA, NA} },

/*line 140*/{(0x1F), 1, USEM_REG_SLEEP_THREADS_VALID,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"USEM: There are sleeping threads",
	{NA, NA, 0, NA, NA, NA} },

/*line 141*/{(0x1F), 1, XSEM_REG_SLEEP_THREADS_VALID,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"XSEM: There are sleeping threads",
	{NA, NA, 0, NA, NA, NA} },

/*line 142*/{(0x1F), 1, CSEM_REG_SLOW_EXT_STORE_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CSEM: External store FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 143*/{(0x1F), 1, TSEM_REG_SLOW_EXT_STORE_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"TSEM: External store FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 144*/{(0x1F), 1, USEM_REG_SLOW_EXT_STORE_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"USEM: External store FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 145*/{(0x1F), 1, XSEM_REG_SLOW_EXT_STORE_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"XSEM: External store FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 146*/{(0x1F), 1, CSDM_REG_SYNC_PARSER_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CSDM: Parser serial FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 147*/{(0x1F), 1, TSDM_REG_SYNC_PARSER_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"TSDM: Parser serial FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 148*/{(0x1F), 1, USDM_REG_SYNC_PARSER_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"USDM: Parser serial FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 149*/{(0x1F), 1, XSDM_REG_SYNC_PARSER_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"XSDM: Parser serial FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 150*/{(0x1F), 1, CSDM_REG_SYNC_SYNC_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CSDM: Parser SYNC serial FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 151*/{(0x1F), 1, TSDM_REG_SYNC_SYNC_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"TSDM: Parser SYNC serial FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 152*/{(0x1F), 1, USDM_REG_SYNC_SYNC_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"USDM: Parser SYNC serial FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 153*/{(0x1F), 1, XSDM_REG_SYNC_SYNC_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"XSDM: Parser SYNC serial FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 154*/{(0x1F), 1, CSDM_REG_RSP_PXP_CTRL_RDATA_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CSDM: pxp_ctrl rd_data fifo is not empty in sdm_dma_rsp block",
	{NA, NA, 1, NA, NA, NA} },

/*line 155*/{(0x1F), 1, TSDM_REG_RSP_PXP_CTRL_RDATA_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"TSDM: pxp_ctrl rd_data fifo is not empty in sdm_dma_rsp block",
	{NA, NA, 1, NA, NA, NA} },

/*line 156*/{(0x1F), 1, USDM_REG_RSP_PXP_CTRL_RDATA_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"USDM: pxp_ctrl rd_data fifo is not empty in sdm_dma_rsp block",
	{NA, NA, 1, NA, NA, NA} },

/*line 157*/{(0x1F), 1, XSDM_REG_RSP_PXP_CTRL_RDATA_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"XSDM: pxp_ctrl rd_data fifo is not empty in sdm_dma_rsp block",
	{NA, NA, 1, NA, NA, NA} },

/*line 158*/{(0x1F), 1, DORQ_REG_DQ_FILL_LVLF,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DORQ: DORQ queue is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 159*/{(0x1F), 1, CFC_REG_CFC_INT_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"CFC: Interrupt status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 160*/{(0x1F), 1, CDU_REG_CDU_INT_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"CDU: Interrupt status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 161*/{(0x1F), 1, CCM_REG_CCM_INT_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"CCM: Interrupt status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 162*/{(0x1F), 1, TCM_REG_TCM_INT_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"TCM: Interrupt status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 163*/{(0x1F), 1, UCM_REG_UCM_INT_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"UCM: Interrupt status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 164*/{(0x1F), 1, XCM_REG_XCM_INT_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"XCM: Interrupt status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 165*/{(0xF), 1, PBF_REG_PBF_INT_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"PBF: Interrupt status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 166*/{(0x1F), 1, TM_REG_TM_INT_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"TIMERS: Interrupt status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 167*/{(0x1F), 1, DORQ_REG_DORQ_INT_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"DORQ: Interrupt status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 168*/{(0x1F), 1, SRC_REG_SRC_INT_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"SRCH: Interrupt status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 169*/{(0x1F), 1, PRS_REG_PRS_INT_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"PRS: Interrupt status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 170*/{(0x1F), 1, BRB1_REG_BRB1_INT_STS,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"BRB1: Interrupt status is not 0",
	{NA, NA, ~0xFC00, 0, NA, NA} },

/*line 171*/{(0x1F), 1, GRCBASE_XPB + PB_REG_PB_INT_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"XPB: Interrupt status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 172*/{(0x1F), 1, GRCBASE_UPB + PB_REG_PB_INT_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"UPB: Interrupt status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 173*/{(0x1), 1, PXP2_REG_PXP2_INT_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP2: Interrupt status 0 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 174*/{(0x1E), 1, PXP2_REG_PXP2_INT_STS_0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP2: Interrupt status 0 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 175*/{(0x1E), 1, PXP2_REG_PXP2_INT_STS_1,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP2: Interrupt status 1 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 176*/{(0x1F), 1, QM_REG_QM_INT_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"QM: Interrupt status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 177*/{(0x1F), 1, PXP_REG_PXP_INT_STS_0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP: P0 Interrupt status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 178*/{(0x1F), 1, PXP_REG_PXP_INT_STS_1,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP: P1 Interrupt status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 179*/{(0x1C), 1, PGLUE_B_REG_PGLUE_B_INT_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: Interrupt status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 180*/{(0x1F), 1, DORQ_REG_RSPA_CRD_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DORQ: Credit to XCM is not full",
	{NA, NA, 2, NA, NA, NA} },

/*line 181*/{(0x1F), 1, DORQ_REG_RSPB_CRD_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"DORQ: Credit to UCM is not full",
	{NA, NA, 2, NA, NA, NA} },

/*line 182*/{(0x3), 1, QM_REG_VOQCRDERRREG,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"QM: Credit error register is not 0 (byte or credit overflow/underflow)",
	{NA, NA, 0, NA, NA, NA} },

/*line 183*/{(0x1F), 1, DORQ_REG_DQ_FULL_ST,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"DORQ: DORQ queue is full",
	{NA, NA, 0, NA, NA, NA} },

/*line 184*/{(0x1F), 1, MISC_REG_AEU_AFTER_INVERT_1_FUNC_0,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"AEU: P0 AFTER_INVERT_1 is not 0",
	{NA, NA, ~0xCFFC, 0, NA, NA} },

/*line 185*/{(0x1F), 1, MISC_REG_AEU_AFTER_INVERT_2_FUNC_0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"AEU: P0 AFTER_INVERT_2 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 186*/{(0x1F), 1, MISC_REG_AEU_AFTER_INVERT_3_FUNC_0,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"AEU: P0 AFTER_INVERT_3 is not 0",
	{NA, NA, ~0xFFFF0000, 0, NA, NA} },

/*line 187*/{(0x1F), 1, MISC_REG_AEU_AFTER_INVERT_4_FUNC_0,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"AEU: P0 AFTER_INVERT_4 is not 0",
	{NA, NA, ~0x801FFFFF, 0, NA, NA} },

/*line 188*/{(0x3), 1, MISC_REG_AEU_AFTER_INVERT_1_FUNC_1,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"AEU: P1 AFTER_INVERT_1 is not 0",
	{NA, NA, ~0xCFFC, 0, NA, NA} },

/*line 189*/{(0x3), 1, MISC_REG_AEU_AFTER_INVERT_2_FUNC_1,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"AEU: P1 AFTER_INVERT_2 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 190*/{(0x3), 1, MISC_REG_AEU_AFTER_INVERT_3_FUNC_1,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"AEU: P1 AFTER_INVERT_3 is not 0",
	{NA, NA, ~0xFFFF0000, 0, NA, NA} },

/*line 191*/{(0x3), 1, MISC_REG_AEU_AFTER_INVERT_4_FUNC_1,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"AEU: P1 AFTER_INVERT_4 is not 0",
	{NA, NA, ~0x801FFFFF, 0, NA, NA} },

/*line 192*/{(0x1F), 1, MISC_REG_AEU_AFTER_INVERT_1_MCP,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"AEU: MCP AFTER_INVERT_1 is not 0",
	{NA, NA, ~0xCFFC, 0, NA, NA} },

/*line 193*/{(0x1F), 1, MISC_REG_AEU_AFTER_INVERT_2_MCP,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"AEU: MCP AFTER_INVERT_2 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 194*/{(0x1F), 1, MISC_REG_AEU_AFTER_INVERT_3_MCP,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"AEU: MCP AFTER_INVERT_3 is not 0",
	{NA, NA, ~0xFFFF0000, 0, NA, NA} },

/*line 195*/{(0x1F), 1, MISC_REG_AEU_AFTER_INVERT_4_MCP,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"AEU: MCP AFTER_INVERT_4 is not 0",
	{NA, NA, ~0x801FFFFF, 0, NA, NA} },

/*line 196*/{(0xF), 5, PBF_REG_P0_CREDIT,
	PBF_REG_P0_INIT_CRD, 1, 0, pneq_r2,
	PBF_REG_DISABLE_NEW_TASK_PROC_P0, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: P0 credit is not equal to init_crd",
	{NA, NA, NA, NA, NA, NA} },

/*line 197*/{(0xF), 5, PBF_REG_P1_CREDIT,
	PBF_REG_P1_INIT_CRD, 1, 0, pneq_r2,
	PBF_REG_DISABLE_NEW_TASK_PROC_P1, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: P1 credit is not equal to init_crd",
	{NA, NA, NA, NA, NA, NA} },

/*line 198*/{(0xF), 3, PBF_REG_P4_CREDIT,
	PBF_REG_P4_INIT_CRD, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: P4 credit is not equal to init_crd",
	{NA, NA, NA, NA, NA, NA} },

/*line 199*/{(0x10), 5, PBF_REG_CREDIT_Q0,
	PBF_REG_INIT_CRD_Q0, 1, 0, pneq_r2,
	PBF_REG_DISABLE_NEW_TASK_PROC_Q0, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: Q0 credit is not equal to init_crd",
	{NA, NA, NA, NA, NA, NA} },

/*line 200*/{(0x10), 5, PBF_REG_CREDIT_Q1,
	PBF_REG_INIT_CRD_Q1, 1, 0, pneq_r2,
	PBF_REG_DISABLE_NEW_TASK_PROC_Q1, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: Q1 credit is not equal to init_crd",
	{NA, NA, NA, NA, NA, NA} },

/*line 201*/{(0x10), 5, PBF_REG_CREDIT_Q2,
	PBF_REG_INIT_CRD_Q2, 1, 0, pneq_r2,
	PBF_REG_DISABLE_NEW_TASK_PROC_Q2, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: Q2 credit is not equal to init_crd",
	{NA, NA, NA, NA, NA, NA} },

/*line 202*/{(0x10), 5, PBF_REG_CREDIT_Q3,
	PBF_REG_INIT_CRD_Q3, 1, 0, pneq_r2,
	PBF_REG_DISABLE_NEW_TASK_PROC_Q3, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: Q3 credit is not equal to init_crd",
	{NA, NA, NA, NA, NA, NA} },

/*line 203*/{(0x10), 5, PBF_REG_CREDIT_Q4,
	PBF_REG_INIT_CRD_Q4, 1, 0, pneq_r2,
	PBF_REG_DISABLE_NEW_TASK_PROC_Q4, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: Q4 credit is not equal to init_crd",
	{NA, NA, NA, NA, NA, NA} },

/*line 204*/{(0x10), 5, PBF_REG_CREDIT_Q5,
	PBF_REG_INIT_CRD_Q5, 1, 0, pneq_r2,
	PBF_REG_DISABLE_NEW_TASK_PROC_Q5, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: Q5 credit is not equal to init_crd",
	{NA, NA, NA, NA, NA, NA} },

/*line 205*/{(0x10), 3, PBF_REG_CREDIT_LB_Q,
	PBF_REG_INIT_CRD_LB_Q, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: LB Q credit is not equal to init_crd",
	{NA, NA, NA, NA, NA, NA} },

/*line 206*/{(0xF), 1, PBF_REG_P0_TASK_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: P0 task_cnt is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 207*/{(0xF), 1, PBF_REG_P1_TASK_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: P1 task_cnt is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 208*/{(0xF), 1, PBF_REG_P4_TASK_CNT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: P4 task_cnt is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 209*/{(0x10), 1, PBF_REG_TASK_CNT_Q0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: Q0 task_cnt is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 210*/{(0x10), 1, PBF_REG_TASK_CNT_Q1,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: Q1 task_cnt is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 211*/{(0x10), 1, PBF_REG_TASK_CNT_Q2,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: Q2 task_cnt is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 212*/{(0x10), 1, PBF_REG_TASK_CNT_Q3,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: Q3 task_cnt is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 213*/{(0x10), 1, PBF_REG_TASK_CNT_Q4,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: Q4 task_cnt is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 214*/{(0x10), 1, PBF_REG_TASK_CNT_Q5,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: Q5 task_cnt is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 215*/{(0x10), 1, PBF_REG_TASK_CNT_LB_Q,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PBF: LB Q task_cnt is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 216*/{(0x1F), 1, XCM_REG_CFC_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"XCM: CFC_INIT_CRD is not 1",
	{NA, NA, 1, NA, NA, NA} },

/*line 217*/{(0x1F), 1, UCM_REG_CFC_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"UCM: CFC_INIT_CRD is not 1",
	{NA, NA, 1, NA, NA, NA} },

/*line 218*/{(0x1F), 1, TCM_REG_CFC_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"TCM: CFC_INIT_CRD is not 1",
	{NA, NA, 1, NA, NA, NA} },

/*line 219*/{(0x1F), 1, CCM_REG_CFC_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CCM: CFC_INIT_CRD is not 1",
	{NA, NA, 1, NA, NA, NA} },

/*line 220*/{(0x1F), 1, XCM_REG_XQM_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"XCM: XQM_INIT_CRD is not 32",
	{NA, NA, 32, NA, NA, NA} },

/*line 221*/{(0x1F), 1, UCM_REG_UQM_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"UCM: UQM_INIT_CRD is not 32",
	{NA, NA, 32, NA, NA, NA} },

/*line 222*/{(0x1F), 1, TCM_REG_TQM_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"TCM: TQM_INIT_CRD is not 32",
	{NA, NA, 32, NA, NA, NA} },

/*line 223*/{(0x1F), 1, CCM_REG_CQM_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CCM: CQM_INIT_CRD is not 32",
	{NA, NA, 32, NA, NA, NA} },

/*line 224*/{(0x1F), 1, XCM_REG_TM_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"XCM: TM_INIT_CRD is not 4",
	{NA, NA, 4, NA, NA, NA} },

/*line 225*/{(0x1F), 1, UCM_REG_TM_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"UCM: TM_INIT_CRD is not 4",
	{NA, NA, 4, NA, NA, NA} },

/*line 226*/{(0x1F), 1, XCM_REG_FIC0_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"XCM: FIC0_INIT_CRD is not 64",
	{NA, NA, 64, NA, NA, NA} },

/*line 227*/{(0x1F), 1, UCM_REG_FIC0_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"UCM: FIC0_INIT_CRD is not 64",
	{NA, NA, 64, NA, NA, NA} },

/*line 228*/{(0x1F), 1, TCM_REG_FIC0_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"TCM: FIC0_INIT_CRD is not 64",
	{NA, NA, 64, NA, NA, NA} },

/*line 229*/{(0x1F), 1, CCM_REG_FIC0_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CCM: FIC0_INIT_CRD is not 64",
	{NA, NA, 64, NA, NA, NA} },

/*line 230*/{(0x1F), 1, XCM_REG_FIC1_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"XCM: FIC1_INIT_CRD is not 64",
	{NA, NA, 64, NA, NA, NA} },

/*line 231*/{(0x1F), 1, UCM_REG_FIC1_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"UCM: FIC1_INIT_CRD is not 64",
	{NA, NA, 64, NA, NA, NA} },

/*line 232*/{(0x1F), 1, TCM_REG_FIC1_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"TCM: FIC1_INIT_CRD is not 64",
	{NA, NA, 64, NA, NA, NA} },

/*line 233*/{(0x1F), 1, CCM_REG_FIC1_INIT_CRD,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CCM: FIC1_INIT_CRD is not 64",
	{NA, NA, 64, NA, NA, NA} },

/*line 234*/{(0x1), 1, XCM_REG_XX_FREE,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"XCM: XX_FREE differs from expected 31",
	{NA, NA, 31, NA, NA, NA} },

/*line 235*/{(0x1E), 1, XCM_REG_XX_FREE,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"XCM: XX_FREE differs from expected 32",
	{NA, NA, 32, NA, NA, NA} },

/*line 236*/{(0x1F), 1, UCM_REG_XX_FREE,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"UCM: XX_FREE differs from expected 27",
	{NA, NA, 27, NA, NA, NA} },

/*line 237*/{(0x7), 1, TCM_REG_XX_FREE,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"TCM: XX_FREE differs from expected 32",
	{NA, NA, 32, NA, NA, NA} },

/*line 238*/{(0x18), 1, TCM_REG_XX_FREE,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"TCM: XX_FREE differs from expected 29",
	{NA, NA, 29, NA, NA, NA} },

/*line 239*/{(0x1F), 1, CCM_REG_XX_FREE,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CCM: XX_FREE differs from expected 24",
	{NA, NA, 24, NA, NA, NA} },

/*line 240*/{(0x1F), 1, XSEM_REG_FAST_MEMORY + 0x18000,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"XSEM: FOC0 credit less than initial credit",
	{NA, NA, 0, NA, NA, NA} },

/*line 241*/{(0x1F), 1, XSEM_REG_FAST_MEMORY + 0x18040,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"XSEM: FOC1 credit less than initial credit",
	{NA, NA, 24, NA, NA, NA} },

/*line 242*/{(0x1F), 1, XSEM_REG_FAST_MEMORY + 0x18080,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"XSEM: FOC2 credit less than initial credit",
	{NA, NA, 12, NA, NA, NA} },

/*line 243*/{(0x1F), 1, USEM_REG_FAST_MEMORY + 0x18000,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"USEM: FOC0 credit less than initial credit",
	{NA, NA, 26, NA, NA, NA} },

/*line 244*/{(0x1F), 1, USEM_REG_FAST_MEMORY + 0x18040,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"USEM: FOC1 credit less than initial credit",
	{NA, NA, 78, NA, NA, NA} },

/*line 245*/{(0x1F), 1, USEM_REG_FAST_MEMORY + 0x18080,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"USEM: FOC2 credit less than initial credit",
	{NA, NA, 16, NA, NA, NA} },

/*line 246*/{(0x1F), 1, USEM_REG_FAST_MEMORY + 0x180C0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"USEM: FOC3 credit less than initial credit",
	{NA, NA, 32, NA, NA, NA} },

/*line 247*/{(0x1F), 1, TSEM_REG_FAST_MEMORY + 0x18000,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"TSEM: FOC0 credit less than initial credit",
	{NA, NA, 52, NA, NA, NA} },

/*line 248*/{(0x1F), 1, TSEM_REG_FAST_MEMORY + 0x18040,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"TSEM: FOC1 credit less than initial credit",
	{NA, NA, 24, NA, NA, NA} },

/*line 249*/{(0x1F), 1, TSEM_REG_FAST_MEMORY + 0x18080,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"TSEM: FOC2 credit less than initial credit",
	{NA, NA, 12, NA, NA, NA} },

/*line 250*/{(0x1F), 1, TSEM_REG_FAST_MEMORY + 0x180C0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"TSEM: FOC3 credit less than initial credit",
	{NA, NA, 32, NA, NA, NA} },

/*line 251*/{(0x1F), 1, CSEM_REG_FAST_MEMORY + 0x18000,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CSEM: FOC0 credit less than initial credit",
	{NA, NA, 16, NA, NA, NA} },

/*line 252*/{(0x1F), 1, CSEM_REG_FAST_MEMORY + 0x18040,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CSEM: FOC1 credit less than initial credit",
	{NA, NA, 18, NA, NA, NA} },

/*line 253*/{(0x1F), 1, CSEM_REG_FAST_MEMORY + 0x18080,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CSEM: FOC2 credit less than initial credit",
	{NA, NA, 48, NA, NA, NA} },

/*line 254*/{(0x1F), 1, CSEM_REG_FAST_MEMORY + 0x180C0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"CSEM: FOC3 credit less than initial credit",
	{NA, NA, 14, NA, NA, NA} },

/*line 255*/{(0x1F), 1, PRS_REG_TSDM_CURRENT_CREDIT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PRS: TSDM current credit is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 256*/{(0x1F), 1, PRS_REG_TCM_CURRENT_CREDIT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PRS: TCM current credit is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 257*/{(0x1F), 1, PRS_REG_CFC_LD_CURRENT_CREDIT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PRS: CFC_LD current credit is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 258*/{(0x1F), 1, PRS_REG_CFC_SEARCH_CURRENT_CREDIT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PRS: CFC_SEARCH current credit is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 259*/{(0x1F), 1, PRS_REG_SRC_CURRENT_CREDIT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PRS: SRCH current credit is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 260*/{(0x1F), 1, PRS_REG_PENDING_BRB_PRS_RQ,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PRS: PENDING_BRB_PRS_RQ is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 261*/{(0x1F), 2, PRS_REG_PENDING_BRB_CAC0_RQ,
	NA, 5, 4, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PRS: PENDING_BRB_CAC_RQ is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 262*/{(0x1F), 1, PRS_REG_SERIAL_NUM_STATUS_LSB,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PRS: SERIAL_NUM_STATUS_LSB is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 263*/{(0x1F), 1, PRS_REG_SERIAL_NUM_STATUS_MSB,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"PRS: SERIAL_NUM_STATUS_MSB is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 264*/{(0x1F), 1, CDU_REG_ERROR_DATA,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"CDU: ERROR_DATA is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 265*/{(0x1F), 1, CCM_REG_STORM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"CCM: STORM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 266*/{(0x1F), 1, CCM_REG_CSDM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"CCM: CSDM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 267*/{(0x1F), 1, CCM_REG_TSEM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"CCM: TSEM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 268*/{(0x1F), 1, CCM_REG_XSEM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"CCM: XSEM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 269*/{(0x1F), 1, CCM_REG_USEM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"CCM: USEM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 270*/{(0x1F), 1, CCM_REG_PBF_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"CCM: PBF declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 271*/{(0x1F), 1, TCM_REG_STORM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"TCM: STORM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 272*/{(0x1F), 1, TCM_REG_TSDM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"TCM: TSDM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 273*/{(0x1F), 1, TCM_REG_PRS_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"TCM: PRS declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 274*/{(0x1F), 1, TCM_REG_PBF_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"TCM: PBF declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 275*/{(0x1F), 1, TCM_REG_USEM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"TCM: USEM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 276*/{(0x1F), 1, TCM_REG_CSEM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"TCM: CSEM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 277*/{(0x1F), 1, UCM_REG_STORM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"UCM: STORM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 278*/{(0x1F), 1, UCM_REG_USDM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"UCM: USDM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 279*/{(0x1F), 1, UCM_REG_TSEM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"UCM: TSEM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 280*/{(0x1F), 1, UCM_REG_CSEM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"UCM: CSEM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 281*/{(0x1F), 1, UCM_REG_XSEM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"UCM: XSEM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 282*/{(0x1F), 1, UCM_REG_DORQ_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"UCM: DORQ declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 283*/{(0x1F), 1, XCM_REG_STORM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"XCM: STORM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 284*/{(0x1F), 1, XCM_REG_XSDM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"XCM: XSDM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 285*/{(0x1F), 1, XCM_REG_TSEM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"XCM: TSEM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 286*/{(0x1F), 1, XCM_REG_CSEM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"XCM: CSEM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 287*/{(0x1F), 1, XCM_REG_USEM_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"XCM: USEM declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 288*/{(0x1F), 1, XCM_REG_DORQ_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"XCM: DORQ declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 289*/{(0x1F), 1, XCM_REG_PBF_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"XCM: PBF declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 290*/{(0x1F), 1, XCM_REG_NIG0_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"XCM: NIG0 declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 291*/{(0x1F), 1, XCM_REG_NIG1_LENGTH_MIS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"XCM: NIG1 declared message length unequal to actual",
	{NA, NA, 0, NA, NA, NA} },

/*line 292*/{(0x1F), 1, QM_REG_XQM_WRC_FIFOLVL,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: XQM wrc_fifolvl is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 293*/{(0x1F), 1, QM_REG_UQM_WRC_FIFOLVL,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: UQM wrc_fifolvl is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 294*/{(0x1F), 1, QM_REG_TQM_WRC_FIFOLVL,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: TQM wrc_fifolvl is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 295*/{(0x1F), 1, QM_REG_CQM_WRC_FIFOLVL,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: CQM wrc_fifolvl is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 296*/{(0x1F), 1, QM_REG_QSTATUS_LOW,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: QSTATUS_LOW is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 297*/{(0x1F), 1, QM_REG_QSTATUS_HIGH,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: QSTATUS_HIGH is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 298*/{(0x1F), 1, QM_REG_PAUSESTATE0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: PAUSESTATE0 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 299*/{(0x1F), 1, QM_REG_PAUSESTATE1,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: PAUSESTATE1 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 300*/{(0x1F), 1, QM_REG_OVFQNUM,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"QM: OVFQNUM is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 301*/{(0x1F), 1, QM_REG_OVFERROR,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"QM: OVFERROR is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 302*/{(0x1F), 6, QM_REG_PTRTBL,
	NA, 64, 8, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: read and write variables not equal",
	{NA, NA, NA, NA, NA, NA} },

/*line 303*/{(0x1F), 1, BRB1_REG_BRB1_PRTY_STS,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"BRB1: parity status is not 0",
	{NA, NA, ~0x8, 0, NA, NA} },

/*line 304*/{(0x1F), 1, CDU_REG_CDU_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"CDU: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 305*/{(0x1F), 1, CFC_REG_CFC_PRTY_STS,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"CFC: parity status is not 0",
	{NA, NA, ~0x2, 0, NA, NA} },

/*line 306*/{(0x1F), 1, CSDM_REG_CSDM_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"CSDM: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 307*/{(0x3), 1, DBG_REG_DBG_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"DBG: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 308*/{(0x1F), 1, DMAE_REG_DMAE_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"DMAE: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 309*/{(0x1F), 1, DORQ_REG_DORQ_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"DORQ: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 310*/{(0x1), 1, TCM_REG_TCM_PRTY_STS,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"TCM: parity status is not 0",
	{NA, NA, ~0x3ffc0, 0, NA, NA} },

/*line 311*/{(0x1E), 1, TCM_REG_TCM_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"TCM: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 312*/{(0x1), 1, CCM_REG_CCM_PRTY_STS,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"CCM: parity status is not 0",
	{NA, NA, ~0x3ffc0, 0, NA, NA} },

/*line 313*/{(0x1E), 1, CCM_REG_CCM_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"CCM: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 314*/{(0x1), 1, UCM_REG_UCM_PRTY_STS,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"UCM: parity status is not 0",
	{NA, NA, ~0x3ffc0, 0, NA, NA} },

/*line 315*/{(0x1E), 1, UCM_REG_UCM_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"UCM: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 316*/{(0x1), 1, XCM_REG_XCM_PRTY_STS,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"XCM: parity status is not 0",
	{NA, NA, ~0x3ffc0, 0, NA, NA} },

/*line 317*/{(0x1E), 1, XCM_REG_XCM_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"XCM: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 318*/{(0x1), 1, HC_REG_HC_PRTY_STS,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"HC: parity status is not 0",
	{NA, NA, ~0x1, 0, NA, NA} },

/*line 319*/{(0x1), 1, MISC_REG_MISC_PRTY_STS,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"MISC: parity status is not 0",
	{NA, NA, ~0x1, 0, NA, NA} },

/*line 320*/{(0x1F), 1, PRS_REG_PRS_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PRS: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 321*/{(0x1F), 1, PXP_REG_PXP_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 322*/{(0x1F), 1, QM_REG_QM_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"QM: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 323*/{(0x1), 1, SRC_REG_SRC_PRTY_STS,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"SRCH: parity status is not 0",
	{NA, NA, ~0x4, 0, NA, NA} },

/*line 324*/{(0x1F), 1, TSDM_REG_TSDM_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"TSDM: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 325*/{(0x1F), 1, USDM_REG_USDM_PRTY_STS,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"USDM: parity status is not 0",
	{NA, NA, ~0x20, 0, NA, NA} },

/*line 326*/{(0x1F), 1, XSDM_REG_XSDM_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"XSDM: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 327*/{(0x1F), 1, GRCBASE_XPB + PB_REG_PB_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"XPB: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 328*/{(0x1F), 1, GRCBASE_UPB + PB_REG_PB_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"UPB: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 329*/{(0x1F), 1, CSEM_REG_CSEM_PRTY_STS_0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"CSEM: parity status 0 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 330*/{(0x1), 1, PXP2_REG_PXP2_PRTY_STS_0,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"PXP2: parity status 0 is not 0",
	{NA, NA, ~0xfff40020, 0, NA, NA} },

/*line 331*/{(0x1E), 1, PXP2_REG_PXP2_PRTY_STS_0,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"PXP2: parity status 0 is not 0",
	{NA, NA, ~0x20, 0, NA, NA} },

/*line 332*/{(0x1F), 1, TSEM_REG_TSEM_PRTY_STS_0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"TSEM: parity status 0 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 333*/{(0x1F), 1, USEM_REG_USEM_PRTY_STS_0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"USEM: parity status 0 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 334*/{(0x1F), 1, XSEM_REG_XSEM_PRTY_STS_0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"XSEM: parity status 0 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 335*/{(0x1F), 1, CSEM_REG_CSEM_PRTY_STS_1,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"CSEM: parity status 1 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 336*/{(0x1), 1, PXP2_REG_PXP2_PRTY_STS_1,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"PXP2: parity status 1 is not 0",
	{NA, NA, ~0x20, 0, NA, NA} },

/*line 337*/{(0x1E), 1, PXP2_REG_PXP2_PRTY_STS_1,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PXP2: parity status 1 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 338*/{(0x1F), 1, TSEM_REG_TSEM_PRTY_STS_1,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"TSEM: parity status 1 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 339*/{(0x1F), 1, USEM_REG_USEM_PRTY_STS_1,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"USEM: parity status 1 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 340*/{(0x1F), 1, XSEM_REG_XSEM_PRTY_STS_1,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"XSEM: parity status 1 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 341*/{(0x1C), 1, PGLUE_B_REG_PGLUE_B_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGLUE_B: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 342*/{(0x2), 2, QM_REG_QTASKCTR_EXT_A_0,
	NA, 64, 4, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: Q_EXT_A (upper 64 queues), Queue is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 343*/{(0x2), 1, QM_REG_QSTATUS_LOW_EXT_A,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"QM: QSTATUS_LOW_EXT_A is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 344*/{(0x2), 1, QM_REG_QSTATUS_HIGH_EXT_A,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"QM: QSTATUS_HIGH_EXT_A is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 345*/{(0x1E), 1, QM_REG_PAUSESTATE2,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: PAUSESTATE2 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 346*/{(0x1E), 1, QM_REG_PAUSESTATE3,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: PAUSESTATE3 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 347*/{(0x2), 1, QM_REG_PAUSESTATE4,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"QM: PAUSESTATE4 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 348*/{(0x2), 1, QM_REG_PAUSESTATE5,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"QM: PAUSESTATE5 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 349*/{(0x2), 1, QM_REG_PAUSESTATE6,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"QM: PAUSESTATE6 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 350*/{(0x2), 1, QM_REG_PAUSESTATE7,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"QM: PAUSESTATE7 is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 351*/{(0x2), 6, QM_REG_PTRTBL_EXT_A,
	NA, 64, 8, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: read and write variables not equal in ext table",
	{NA, NA, NA, NA, NA, NA} },

/*line 352*/{(0x1E), 1, MISC_REG_AEU_SYS_KILL_OCCURRED,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"MISC: system kill occurred;",
	{NA, NA, 0, NA, NA, NA} },

/*line 353*/{(0x1E), 1, MISC_REG_AEU_SYS_KILL_STATUS_0,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"MISC: system kill occurred; status_0 register",
	{NA, NA, 0, NA, NA, NA} },

/*line 354*/{(0x1E), 1, MISC_REG_AEU_SYS_KILL_STATUS_1,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"MISC: system kill occurred; status_1 register",
	{NA, NA, 0, NA, NA, NA} },

/*line 355*/{(0x1E), 1, MISC_REG_AEU_SYS_KILL_STATUS_2,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"MISC: system kill occurred; status_2 register",
	{NA, NA, 0, NA, NA, NA} },

/*line 356*/{(0x1E), 1, MISC_REG_AEU_SYS_KILL_STATUS_3,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"MISC: system kill occurred; status_3 register",
	{NA, NA, 0, NA, NA, NA} },

/*line 357*/{(0x1E), 1, MISC_REG_PCIE_HOT_RESET,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_WARNING,
	"MISC: pcie_rst_b was asserted without perst assertion",
	{NA, NA, 0, NA, NA, NA} },

/*line 358*/{(0x1F), 1, NIG_REG_NIG_INT_STS_0,
	NA, NA, NA, pand_neq,
	NA, IDLE_CHK_ERROR,
	"NIG: interrupt 0 is active",
	{NA, NA, ~0x300, 0, NA, NA} },

/*line 359*/{(0x1F), 1, NIG_REG_NIG_INT_STS_0,
	NA, NA, NA, peq,
	NA, IDLE_CHK_WARNING,
	"NIG: Access to BMAC while not active. If tested on FPGA, ignore this warning",
	{NA, NA, 0x300, NA, NA, NA} },

/*line 360*/{(0x1F), 1, NIG_REG_NIG_INT_STS_1,
	NA, NA, NA, pand_neq,
	NA, IDLE_CHK_ERROR,
	"NIG: interrupt 1 is active",
	{NA, NA, 0x783FF03, 0, NA, NA} },

/*line 361*/{(0x1F), 1, NIG_REG_NIG_INT_STS_1,
	NA, NA, NA, pand_neq,
	NA, IDLE_CHK_WARNING,
	"NIG: port cos was paused too long",
	{NA, NA, ~0x783FF0F, 0, NA, NA} },

/*line 362*/{(0x1F), 1, NIG_REG_NIG_INT_STS_1,
	NA, NA, NA, pand_neq,
	NA, IDLE_CHK_WARNING,
	"NIG: Got packets w/o Outer-VLAN in MF mode",
	{NA, NA, 0xC, 0, NA, NA} },

/*line 363*/{(0x2), 1, NIG_REG_NIG_PRTY_STS,
	NA, NA, NA, pand_neq,
	NA, IDLE_CHK_ERROR,
	"NIG: parity interrupt is active",
	{NA, NA, ~0xFFC00000, 0, NA, NA} },

/*line 364*/{(0x1C), 1, NIG_REG_NIG_PRTY_STS_0,
	NA, NA, NA, pand_neq,
	NA, IDLE_CHK_ERROR,
	"NIG: parity 0 interrupt is active",
	{NA, NA, ~0xFFC00000, 0, NA, NA} },

/*line 365*/{(0x4), 1, NIG_REG_NIG_PRTY_STS_1,
	NA, NA, NA, pand_neq,
	NA, IDLE_CHK_ERROR,
	"NIG: parity 1 interrupt is active",
	{NA, NA, 0xff, 0, NA, NA} },

/*line 366*/{(0x18), 1, NIG_REG_NIG_PRTY_STS_1,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"NIG: parity 1 interrupt is active",
	{NA, NA, 0, NA, NA, NA} },

/*line 367*/{(0x1F), 1, TSEM_REG_TSEM_INT_STS_0,
	NA, NA, NA, pand_neq,
	NA, IDLE_CHK_WARNING,
	"TSEM: interrupt 0 is active",
	{NA, NA, ~0x10000000, 0, NA, NA} },

/*line 368*/{(0x1F), 1, TSEM_REG_TSEM_INT_STS_0,
	NA, NA, NA, peq,
	NA, IDLE_CHK_WARNING,
	"TSEM: interrupt 0 is active",
	{NA, NA, 0x10000000, NA, NA, NA} },

/*line 369*/{(0x1F), 1, TSEM_REG_TSEM_INT_STS_1,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"TSEM: interrupt 1 is active",
	{NA, NA, 0, NA, NA, NA} },

/*line 370*/{(0x1F), 1, CSEM_REG_CSEM_INT_STS_0,
	NA, NA, NA, pand_neq,
	NA, IDLE_CHK_WARNING,
	"CSEM: interrupt 0 is active",
	{NA, NA, ~0x10000000, 0, NA, NA} },

/*line 371*/{(0x1F), 1, CSEM_REG_CSEM_INT_STS_0,
	NA, NA, NA, peq,
	NA, IDLE_CHK_WARNING,
	"CSEM: interrupt 0 is active",
	{NA, NA, 0x10000000, NA, NA, NA} },

/*line 372*/{(0x1F), 1, CSEM_REG_CSEM_INT_STS_1,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"CSEM: interrupt 1 is active",
	{NA, NA, 0, NA, NA, NA} },

/*line 373*/{(0x1F), 1, USEM_REG_USEM_INT_STS_0,
	NA, NA, NA, pand_neq,
	NA, IDLE_CHK_WARNING,
	"USEM: interrupt 0 is active",
	{NA, NA, ~0x10000000, 0, NA, NA} },

/*line 374*/{(0x1F), 1, USEM_REG_USEM_INT_STS_0,
	NA, NA, NA, peq,
	NA, IDLE_CHK_WARNING,
	"USEM: interrupt 0 is active",
	{NA, NA, 0x10000000, NA, NA, NA} },

/*line 375*/{(0x1F), 1, USEM_REG_USEM_INT_STS_1,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"USEM: interrupt 1 is active",
	{NA, NA, 0, NA, NA, NA} },

/*line 376*/{(0x1F), 1, XSEM_REG_XSEM_INT_STS_0,
	NA, NA, NA, pand_neq,
	NA, IDLE_CHK_WARNING,
	"XSEM: interrupt 0 is active",
	{NA, NA, ~0x10000000, 0, NA, NA} },

/*line 377*/{(0x1F), 1, XSEM_REG_XSEM_INT_STS_0,
	NA, NA, NA, peq,
	NA, IDLE_CHK_WARNING,
	"XSEM: interrupt 0 is active",
	{NA, NA, 0x10000000, NA, NA, NA} },

/*line 378*/{(0x1F), 1, XSEM_REG_XSEM_INT_STS_1,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"XSEM: interrupt 1 is active",
	{NA, NA, 0, NA, NA, NA} },

/*line 379*/{(0x1F), 1, TSDM_REG_TSDM_INT_STS_0,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"TSDM: interrupt 0 is active",
	{NA, NA, 0, NA, NA, NA} },

/*line 380*/{(0x1F), 1, TSDM_REG_TSDM_INT_STS_1,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"TSDM: interrupt 0 is active",
	{NA, NA, 0, NA, NA, NA} },

/*line 381*/{(0x1F), 1, CSDM_REG_CSDM_INT_STS_0,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"CSDM: interrupt 0 is active",
	{NA, NA, 0, NA, NA, NA} },

/*line 382*/{(0x1F), 1, CSDM_REG_CSDM_INT_STS_1,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"CSDM: interrupt 0 is active",
	{NA, NA, 0, NA, NA, NA} },

/*line 383*/{(0x1F), 1, USDM_REG_USDM_INT_STS_0,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"USDM: interrupt 0 is active",
	{NA, NA, 0, NA, NA, NA} },

/*line 384*/{(0x1F), 1, USDM_REG_USDM_INT_STS_1,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"USDM: interrupt 0 is active",
	{NA, NA, 0, NA, NA, NA} },

/*line 385*/{(0x1F), 1, XSDM_REG_XSDM_INT_STS_0,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"XSDM: interrupt 0 is active",
	{NA, NA, 0, NA, NA, NA} },

/*line 386*/{(0x1F), 1, XSDM_REG_XSDM_INT_STS_1,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_ERROR,
	"XSDM: interrupt 0 is active",
	{NA, NA, 0, NA, NA, NA} },

/*line 387*/{(0x2), 1, HC_REG_HC_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"HC: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 388*/{(0x1E), 1, MISC_REG_MISC_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"MISC: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 389*/{(0x1E), 1, SRC_REG_SRC_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"SRCH: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 390*/{(0xC), 3, QM_REG_BYTECRD0,
	QM_REG_BYTECRDINITVAL, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: Byte credit 0 is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 391*/{(0xC), 3, QM_REG_BYTECRD1,
	QM_REG_BYTECRDINITVAL, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: Byte credit 1 is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 392*/{(0xC), 3, QM_REG_BYTECRD2,
	QM_REG_BYTECRDINITVAL, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: Byte credit 2 is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 393*/{(0x1C), 1, QM_REG_VOQCRDERRREG,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"QM: VOQ credit error register is not 0 (VOQ credit overflow/underflow)",
	{NA, NA, 0xFFFF, 0, NA, NA} },

/*line 394*/{(0x1C), 1, QM_REG_BYTECRDERRREG,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"QM: Byte credit error register is not 0 (Byte credit overflow/underflow)",
	{NA, NA, 0xFFF, 0, NA, NA} },

/*line 395*/{(0x1C), 1, PGLUE_B_REG_FLR_REQUEST_VF_31_0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGL: FLR request is set for VF addresses 31-0",
	{NA, NA, 0, NA, NA, NA} },

/*line 396*/{(0x1C), 1, PGLUE_B_REG_FLR_REQUEST_VF_63_32,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGL: FLR request is set for VF addresses 63-32",
	{NA, NA, 0, NA, NA, NA} },

/*line 397*/{(0x1C), 1, PGLUE_B_REG_FLR_REQUEST_VF_95_64,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGL: FLR request is set for VF addresses 95-64",
	{NA, NA, 0, NA, NA, NA} },

/*line 398*/{(0x1C), 1, PGLUE_B_REG_FLR_REQUEST_VF_127_96,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGL: FLR request is set for VF addresses 127-96",
	{NA, NA, 0, NA, NA, NA} },

/*line 399*/{(0x1C), 1, PGLUE_B_REG_FLR_REQUEST_PF_7_0,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGL: FLR request is set for PF addresses 7-0",
	{NA, NA, 0, NA, NA, NA} },

/*line 400*/{(0x1C), 1, PGLUE_B_REG_SR_IOV_DISABLED_REQUEST,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGL: SR-IOV disable request is set",
	{NA, NA, 0, NA, NA, NA} },

/*line 401*/{(0x1C), 1, PGLUE_B_REG_CFG_SPACE_A_REQUEST,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGL: Cfg-Space A request is set",
	{NA, NA, 0, NA, NA, NA} },

/*line 402*/{(0x1C), 1, PGLUE_B_REG_CFG_SPACE_B_REQUEST,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"PGL: Cfg-Space B request is set",
	{NA, NA, 0, NA, NA, NA} },

/*line 403*/{(0x1C), 1, IGU_REG_ERROR_HANDLING_DATA_VALID,
	NA, NA, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"IGU: some unauthorized commands arrived to the IGU. Use igu_dump_fifo utility for more details",
	{NA, NA, 0, NA, NA, NA} },

/*line 404*/{(0x1C), 1, IGU_REG_ATTN_WRITE_DONE_PENDING,
	NA, NA, NA, pneq,
	NA, IDLE_CHK_WARNING,
	"IGU attention message write done pending is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 405*/{(0x1C), 1, IGU_REG_WRITE_DONE_PENDING,
	NA, 5, 4, pneq,
	NA, IDLE_CHK_WARNING,
	"IGU MSI/MSIX message write done pending is not empty",
	{NA, NA, 0, NA, NA, NA} },

/*line 406*/{(0x1C), 1, IGU_REG_IGU_PRTY_STS,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"IGU: parity status is not 0",
	{NA, NA, 0, NA, NA, NA} },

/*line 407*/{(0x1E), 3, MISC_REG_GRC_TIMEOUT_ATTN,
	MISC_REG_AEU_AFTER_INVERT_4_FUNC_0, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"MISC_REG_GRC_TIMEOUT_ATTN: GRC timeout attention parameters (FUNC_0)",
	{NA, NA, 0x4000000, 0, NA, NA} },

/*line 408*/{(0x1C), 3, MISC_REG_GRC_TIMEOUT_ATTN_FULL_FID,
	MISC_REG_AEU_AFTER_INVERT_4_FUNC_0, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"MISC_REG_GRC_TIMEOUT_ATTN_FULL_FID: GRC timeout attention FID (FUNC_0)",
	{NA, NA, 0x4000000, 0, NA, NA} },

/*line 409*/{(0x1E), 3, MISC_REG_GRC_TIMEOUT_ATTN,
	MISC_REG_AEU_AFTER_INVERT_4_FUNC_1, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"MISC_REG_GRC_TIMEOUT_ATTN: GRC timeout attention parameters (FUNC_1)",
	{NA, NA, 0x4000000, 0, NA, NA} },

/*line 410*/{(0x1C), 3, MISC_REG_GRC_TIMEOUT_ATTN_FULL_FID,
	MISC_REG_AEU_AFTER_INVERT_4_FUNC_1, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"MISC_REG_GRC_TIMEOUT_ATTN_FULL_FID: GRC timeout attention FID (FUNC_1)",
	{NA, NA, 0x4000000, 0, NA, NA} },

/*line 411*/{(0x1E), 3, MISC_REG_GRC_TIMEOUT_ATTN,
	MISC_REG_AEU_AFTER_INVERT_4_MCP, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"MISC_REG_GRC_TIMEOUT_ATTN: GRC timeout attention parameters (MCP)",
	{NA, NA, 0x4000000, 0, NA, NA} },

/*line 412*/{(0x1C), 3, MISC_REG_GRC_TIMEOUT_ATTN_FULL_FID,
	MISC_REG_AEU_AFTER_INVERT_4_MCP, 1, 0, pand_neq,
	NA, IDLE_CHK_ERROR,
	"MISC_REG_GRC_TIMEOUT_ATTN_FULL_FID: GRC timeout attention FID (MCP)",
	{NA, NA, 0x4000000, 0, NA, NA} },

/*line 413*/{(0x1C), 1, IGU_REG_SILENT_DROP,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"Some messages were not executed in the IGU",
	{NA, NA, 0, NA, NA, NA} },

/*line 414*/{(0x1C), 1, PXP2_REG_PSWRQ_BW_CREDIT,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR,
	"PXP2: rq_read_credit and rq_write_credit are not 5",
	{NA, NA, 0x2D, NA, NA, NA} },

/*line 415*/{(0x1C), 1, IGU_REG_SB_CTRL_FSM,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"IGU: block is not in idle. SB_CTRL_FSM should be zero in idle state",
	{NA, NA, 0, NA, NA, NA} },

/*line 416*/{(0x1C), 1, IGU_REG_INT_HANDLE_FSM,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"IGU: block is not in idle. INT_HANDLE_FSM should be zero in idle state",
	{NA, NA, 0, NA, NA, NA} },

/*line 417*/{(0x1C), 1, IGU_REG_ATTN_FSM,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"IGU: block is not in idle. SB_ATTN_FSMshould be zeroor two in idle state",
	{NA, NA, ~0x2, 0, NA, NA} },

/*line 418*/{(0x1C), 1, IGU_REG_CTRL_FSM,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"IGU: block is not in idle. SB_CTRL_FSM should be zero in idle state",
	{NA, NA, ~0x1, 0, NA, NA} },

/*line 419*/{(0x1C), 1, IGU_REG_PXP_ARB_FSM,
	NA, 1, 0, pand_neq,
	NA, IDLE_CHK_WARNING,
	"IGU: block is not in idle. SB_ARB_FSM should be zero in idle state",
	{NA, NA, ~0x1, 0, NA, NA} },

/*line 420*/{(0x1C), 1, IGU_REG_PENDING_BITS_STATUS,
	NA, 5, 4, pneq,
	NA, IDLE_CHK_WARNING,
	"IGU: block is not in idle. There are pending write done",
	{NA, NA, 0, NA, NA, NA} },

/*line 421*/{(0x10), 3, QM_REG_VOQCREDIT_0,
	QM_REG_VOQINITCREDIT_0, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: VOQ_0, VOQ credit is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 422*/{(0x10), 3, QM_REG_VOQCREDIT_1,
	QM_REG_VOQINITCREDIT_1, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: VOQ_1, VOQ credit is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 423*/{(0x10), 3, QM_REG_VOQCREDIT_2,
	QM_REG_VOQINITCREDIT_2, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: VOQ_2, VOQ credit is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 424*/{(0x10), 3, QM_REG_VOQCREDIT_3,
	QM_REG_VOQINITCREDIT_3, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: VOQ_3, VOQ credit is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 425*/{(0x10), 3, QM_REG_VOQCREDIT_4,
	QM_REG_VOQINITCREDIT_4, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: VOQ_4, VOQ credit is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 426*/{(0x10), 3, QM_REG_VOQCREDIT_5,
	QM_REG_VOQINITCREDIT_5, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: VOQ_5, VOQ credit is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 427*/{(0x10), 3, QM_REG_VOQCREDIT_6,
	QM_REG_VOQINITCREDIT_6, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: VOQ_6 (LB VOQ), VOQ credit is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 428*/{(0x10), 3, QM_REG_BYTECRD0,
	QM_REG_BYTECRDINITVAL, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: Byte credit 0 is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 429*/{(0x10), 3, QM_REG_BYTECRD1,
	QM_REG_BYTECRDINITVAL, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: Byte credit 1 is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 430*/{(0x10), 3, QM_REG_BYTECRD2,
	QM_REG_BYTECRDINITVAL, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: Byte credit 2 is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 431*/{(0x10), 3, QM_REG_BYTECRD3,
	QM_REG_BYTECRDINITVAL, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: Byte credit 3 is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 432*/{(0x10), 3, QM_REG_BYTECRD4,
	QM_REG_BYTECRDINITVAL, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: Byte credit 4 is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 433*/{(0x10), 3, QM_REG_BYTECRD5,
	QM_REG_BYTECRDINITVAL, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: Byte credit 5 is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 434*/{(0x10), 3, QM_REG_BYTECRD6,
	QM_REG_BYTECRDINITVAL, 1, 0, pneq_r2,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"QM: Byte credit 6 is not equal to initial credit",
	{NA, NA, NA, NA, NA, NA} },

/*line 435*/{(0x10), 1, QM_REG_FWVOQ0TOHWVOQ,
	NA, 1, 0, peq,
	NA, IDLE_CHK_ERROR,
	"QM: FwVoq0 is mapped to HwVoq7 (non-TX HwVoq)",
	{NA, NA, 0x7, NA, NA, NA} },

/*line 436*/{(0x10), 1, QM_REG_FWVOQ1TOHWVOQ,
	NA, 1, 0, peq,
	NA, IDLE_CHK_ERROR,
	"QM: FwVoq1 is mapped to HwVoq7 (non-TX HwVoq)",
	{NA, NA, 0x7, NA, NA, NA} },

/*line 437*/{(0x10), 1, QM_REG_FWVOQ2TOHWVOQ,
	NA, 1, 0, peq,
	NA, IDLE_CHK_ERROR,
	"QM: FwVoq2 is mapped to HwVoq7 (non-TX HwVoq)",
	{NA, NA, 0x7, NA, NA, NA} },

/*line 438*/{(0x10), 1, QM_REG_FWVOQ3TOHWVOQ,
	NA, 1, 0, peq,
	NA, IDLE_CHK_ERROR,
	"QM: FwVoq3 is mapped to HwVoq7 (non-TX HwVoq)",
	{NA, NA, 0x7, NA, NA, NA} },

/*line 439*/{(0x10), 1, QM_REG_FWVOQ4TOHWVOQ,
	NA, 1, 0, peq,
	NA, IDLE_CHK_ERROR,
	"QM: FwVoq4 is mapped to HwVoq7 (non-TX HwVoq)",
	{NA, NA, 0x7, NA, NA, NA} },

/*line 440*/{(0x10), 1, QM_REG_FWVOQ5TOHWVOQ,
	NA, 1, 0, peq,
	NA, IDLE_CHK_ERROR,
	"QM: FwVoq5 is mapped to HwVoq7 (non-TX HwVoq)",
	{NA, NA, 0x7, NA, NA, NA} },

/*line 441*/{(0x10), 1, QM_REG_FWVOQ6TOHWVOQ,
	NA, 1, 0, peq,
	NA, IDLE_CHK_ERROR,
	"QM: FwVoq6 is mapped to HwVoq7 (non-TX HwVoq)",
	{NA, NA, 0x7, NA, NA, NA} },

/*line 442*/{(0x10), 1, QM_REG_FWVOQ7TOHWVOQ,
	NA, 1, 0, peq,
	NA, IDLE_CHK_ERROR,
	"QM: FwVoq7 is mapped to HwVoq7 (non-TX HwVoq)",
	{NA, NA, 0x7, NA, NA, NA} },

/*line 443*/{(0x1F), 1, NIG_REG_INGRESS_EOP_PORT0_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: Port 0 EOP FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 444*/{(0x1F), 1, NIG_REG_INGRESS_EOP_PORT1_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: Port 1 EOP FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 445*/{(0x1F), 1, NIG_REG_INGRESS_EOP_LB_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: LB EOP FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 446*/{(0x1F), 1, NIG_REG_INGRESS_RMP0_DSCR_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"NIG: Port 0 RX MCP descriptor FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 447*/{(0x1F), 1, NIG_REG_INGRESS_RMP1_DSCR_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"NIG: Port 1 RX MCP descriptor FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 448*/{(0x1F), 1, NIG_REG_INGRESS_LB_PBF_DELAY_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: PBF LB FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 449*/{(0x1F), 1, NIG_REG_EGRESS_MNG0_FIFO_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"NIG: Port 0 TX MCP FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 450*/{(0x1F), 1, NIG_REG_EGRESS_MNG1_FIFO_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"NIG: Port 1 TX MCP FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 451*/{(0x1F), 1, NIG_REG_EGRESS_DEBUG_FIFO_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: Debug FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 452*/{(0x1F), 1, NIG_REG_EGRESS_DELAY0_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: PBF IF0 FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 453*/{(0x1F), 1, NIG_REG_EGRESS_DELAY1_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: PBF IF1 FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 454*/{(0x1F), 1, NIG_REG_LLH0_FIFO_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: Port 0 RX LLH FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 455*/{(0x1F), 1, NIG_REG_LLH1_FIFO_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: Port 1 RX LLH FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 456*/{(0x1C), 1, NIG_REG_P0_TX_MNG_HOST_FIFO_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"NIG: Port 0 TX MCP FIFO for traffic going to the host is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 457*/{(0x1C), 1, NIG_REG_P1_TX_MNG_HOST_FIFO_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"NIG: Port 1 TX MCP FIFO for traffic going to the host is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 458*/{(0x1C), 1, NIG_REG_P0_TLLH_FIFO_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: Port 0 TX LLH FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 459*/{(0x1C), 1, NIG_REG_P1_TLLH_FIFO_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: Port 1 TX LLH FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 460*/{(0x1C), 1, NIG_REG_P0_HBUF_DSCR_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"NIG: Port 0 RX MCP descriptor FIFO for traffic from the host is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 461*/{(0x1C), 1, NIG_REG_P1_HBUF_DSCR_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_WARNING,
	"NIG: Port 1 RX MCP descriptor FIFO for traffic from the host is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 462*/{(0x18), 1, NIG_REG_P0_RX_MACFIFO_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: Port 0 RX MAC interface FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 463*/{(0x18), 1, NIG_REG_P1_RX_MACFIFO_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: Port 1 RX MAC interface FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 464*/{(0x18), 1, NIG_REG_P0_TX_MACFIFO_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: Port 0 TX MAC interface FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 465*/{(0x18), 1, NIG_REG_P1_TX_MACFIFO_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: Port 1 TX MAC interface FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 466*/{(0x10), 1, NIG_REG_EGRESS_DELAY2_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: PBF IF2 FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 467*/{(0x10), 1, NIG_REG_EGRESS_DELAY3_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: PBF IF3 FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 468*/{(0x10), 1, NIG_REG_EGRESS_DELAY4_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: PBF IF4 FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },

/*line 469*/{(0x10), 1, NIG_REG_EGRESS_DELAY5_EMPTY,
	NA, 1, 0, pneq,
	NA, IDLE_CHK_ERROR_NO_TRAFFIC,
	"NIG: PBF IF5 FIFO is not empty",
	{NA, NA, 1, NA, NA, NA} },
};

/* handle self test fails according to severity and type */
static void bnx2x_self_test_log(struct bnx2x *bp, u8 severity, char *message)
{
	switch (severity) {
	case IDLE_CHK_ERROR:
		BNX2X_ERR("ERROR %s", message);
		idle_chk_errors++;
		break;
	case IDLE_CHK_ERROR_NO_TRAFFIC:
		DP(NETIF_MSG_HW, "INFO %s", message);
		break;
	case IDLE_CHK_WARNING:
		DP(NETIF_MSG_HW, "WARNING %s", message);
		idle_chk_warnings++;
		break;
	}
}

/* specific test for QM rd/wr pointers and rd/wr banks */
static void bnx2x_idle_chk6(struct bnx2x *bp,
			    struct st_record *rec, char *message)
{
	u32 rd_ptr, wr_ptr, rd_bank, wr_bank;
	int i;

	for (i = 0; i < rec->loop; i++) {
		/* read regs */
		rec->pred_args.val1 =
			REG_RD(bp, rec->reg1 + i * rec->incr);
		rec->pred_args.val2 =
			REG_RD(bp, rec->reg1 + i * rec->incr + 4);

		/* calc read and write pointers */
		rd_ptr = ((rec->pred_args.val1 & 0x3FFFFFC0) >> 6);
		wr_ptr = ((((rec->pred_args.val1 & 0xC0000000) >> 30) & 0x3) |
			((rec->pred_args.val2 & 0x3FFFFF) << 2));

		/* perfrom pointer test */
		if (rd_ptr != wr_ptr) {
			snprintf(message, MAX_FAIL_MSG,
				 "QM: PTRTBL entry %d- rd_ptr is not equal to wr_ptr. Values are 0x%x and 0x%x\n",
				 i, rd_ptr, wr_ptr);
			bnx2x_self_test_log(bp, rec->severity, message);
		}

		/* calculate read and write banks */
		rd_bank = ((rec->pred_args.val1 & 0x30) >> 4);
		wr_bank = (rec->pred_args.val1 & 0x03);

		/* perform bank test */
		if (rd_bank != wr_bank) {
			snprintf(message, MAX_FAIL_MSG,
				 "QM: PTRTBL entry %d - rd_bank is not equal to wr_bank. Values are 0x%x 0x%x\n",
				 i, rd_bank, wr_bank);
			bnx2x_self_test_log(bp, rec->severity, message);
		}
	}
}

/* specific test for cfc info ram and cid cam */
static void bnx2x_idle_chk7(struct bnx2x *bp,
			    struct st_record *rec, char *message)
{
	int i;

	/* iterate through lcids */
	for (i = 0; i < rec->loop; i++) {
		/* make sure cam entry is valid (bit 0) */
		if ((REG_RD(bp, (rec->reg2 + i * 4)) & 0x1) != 0x1)
			continue;

		/* get connection type (multiple reads due to widebus) */
		REG_RD(bp, (rec->reg1 + i * rec->incr));
		REG_RD(bp, (rec->reg1 + i * rec->incr + 4));
		rec->pred_args.val1 =
			REG_RD(bp, (rec->reg1 + i * rec->incr + 8));
		REG_RD(bp, (rec->reg1 + i * rec->incr + 12));

		/* obtain connection type */
		if (is_e1 || is_e1h) {
			/* E1 E1H (bits 4..7) */
			rec->pred_args.val1 &= 0x78;
			rec->pred_args.val1 >>= 3;
		} else {
			/* E2 E3A0 E3B0 (bits 26..29) */
			rec->pred_args.val1 &= 0x1E000000;
			rec->pred_args.val1 >>= 25;
		}

		/* get activity counter value */
		rec->pred_args.val2 = REG_RD(bp, rec->reg3 + i * 4);

		/* validate ac value is legal for con_type at idle state */
		if (rec->bnx2x_predicate(&rec->pred_args)) {
			snprintf(message, MAX_FAIL_MSG,
				 "%s. Values are 0x%x 0x%x\n", rec->fail_msg,
				 rec->pred_args.val1, rec->pred_args.val2);
			bnx2x_self_test_log(bp, rec->severity, message);
		}
	}
}

/* self test procedure
 * scan auto-generated database
 * for each line:
 * 1.	compare chip mask
 * 2.	determine type (according to maro number)
 * 3.	read registers
 * 4.	call predicate
 * 5.	collate results and statistics
 */
int bnx2x_idle_chk(struct bnx2x *bp)
{
	u16 i;				/* loop counter */
	u16 st_ind;			/* self test database access index */
	struct st_record rec;		/* current record variable */
	char message[MAX_FAIL_MSG];	/* message to log */

	/*init stats*/
	idle_chk_errors = 0;
	idle_chk_warnings = 0;

	/*create masks for all chip types*/
	is_e1	= CHIP_IS_E1(bp);
	is_e1h	= CHIP_IS_E1H(bp);
	is_e2	= CHIP_IS_E2(bp);
	is_e3a0	= CHIP_IS_E3A0(bp);
	is_e3b0	= CHIP_IS_E3B0(bp);

	/*database main loop*/
	for (st_ind = 0; st_ind < ST_DB_LINES; st_ind++) {
		rec = st_database[st_ind];

		/*check if test applies to chip*/
		if (!((rec.chip_mask & IDLE_CHK_E1) && is_e1) &&
		    !((rec.chip_mask & IDLE_CHK_E1H) && is_e1h) &&
		    !((rec.chip_mask & IDLE_CHK_E2) && is_e2) &&
		    !((rec.chip_mask & IDLE_CHK_E3A0) && is_e3a0) &&
		    !((rec.chip_mask & IDLE_CHK_E3B0) && is_e3b0))
			continue;

		/* identify macro */
		switch (rec.macro) {
		case 1:
			/* read single reg and call predicate */
			rec.pred_args.val1 = REG_RD(bp, rec.reg1);
			DP(BNX2X_MSG_IDLE, "mac1 add %x\n", rec.reg1);
			if (rec.bnx2x_predicate(&rec.pred_args)) {
				snprintf(message, sizeof(message),
					 "%s.Value is 0x%x\n", rec.fail_msg,
					 rec.pred_args.val1);
				bnx2x_self_test_log(bp, rec.severity, message);
			}
			break;
		case 2:
			/* read repeatedly starting from reg1 and call
			 * predicate after each read
			 */
			for (i = 0; i < rec.loop; i++) {
				rec.pred_args.val1 =
					REG_RD(bp, rec.reg1 + i * rec.incr);
				DP(BNX2X_MSG_IDLE, "mac2 add %x\n", rec.reg1);
				if (rec.bnx2x_predicate(&rec.pred_args)) {
					snprintf(message, sizeof(message),
						 "%s. Value is 0x%x in loop %d\n",
						 rec.fail_msg,
						 rec.pred_args.val1, i);
					bnx2x_self_test_log(bp, rec.severity,
							    message);
				}
			}
			break;
		case 3:
			/* read two regs and call predicate */
			rec.pred_args.val1 = REG_RD(bp, rec.reg1);
			rec.pred_args.val2 = REG_RD(bp, rec.reg2);
			DP(BNX2X_MSG_IDLE, "mac3 add1 %x add2 %x\n",
			   rec.reg1, rec.reg2);
			if (rec.bnx2x_predicate(&rec.pred_args)) {
				snprintf(message, sizeof(message),
					 "%s. Values are 0x%x 0x%x\n",
					 rec.fail_msg, rec.pred_args.val1,
					 rec.pred_args.val2);
				bnx2x_self_test_log(bp, rec.severity, message);
			}
			break;
		case 4:
			/*unused to-date*/
			for (i = 0; i < rec.loop; i++) {
				rec.pred_args.val1 =
					REG_RD(bp, rec.reg1 + i * rec.incr);
				rec.pred_args.val2 =
					(REG_RD(bp,
						rec.reg2 + i * rec.incr)) >> 1;
				if (rec.bnx2x_predicate(&rec.pred_args)) {
					snprintf(message, sizeof(message),
						 "%s. Values are 0x%x 0x%x in loop %d\n",
						 rec.fail_msg,
						 rec.pred_args.val1,
						 rec.pred_args.val2, i);
					bnx2x_self_test_log(bp, rec.severity,
							    message);
				}
			}
			break;
		case 5:
			/* compare two regs, pending
			 * the value of a condition reg
			 */
			rec.pred_args.val1 = REG_RD(bp, rec.reg1);
			rec.pred_args.val2 = REG_RD(bp, rec.reg2);
			DP(BNX2X_MSG_IDLE, "mac3 add1 %x add2 %x add3 %x\n",
			   rec.reg1, rec.reg2, rec.reg3);
			if (REG_RD(bp, rec.reg3) != 0) {
				if (rec.bnx2x_predicate(&rec.pred_args)) {
					snprintf(message, sizeof(message),
						 "%s. Values are 0x%x 0x%x\n",
						 rec.fail_msg,
						 rec.pred_args.val1,
						 rec.pred_args.val2);
					bnx2x_self_test_log(bp, rec.severity,
							    message);
				}
			}
			break;
		case 6:
			/* compare read and write pointers
			 * and read and write banks in QM
			 */
			bnx2x_idle_chk6(bp, &rec, message);
			break;
		case 7:
			/* compare cfc info cam with cid cam */
			bnx2x_idle_chk7(bp, &rec, message);
			break;
		default:
			DP(BNX2X_MSG_IDLE,
			   "unknown macro in self test data base. macro %d line %d",
			   rec.macro, st_ind);
		}
	}

	/* abort if interface is not running */
	if (!netif_running(bp->dev))
		return idle_chk_errors;

	/* return value accorindg to statistics */
	if (idle_chk_errors == 0) {
		DP(BNX2X_MSG_IDLE,
		   "completed successfully (logged %d warnings)\n",
		   idle_chk_warnings);
	} else {
		BNX2X_ERR("failed (with %d errors, %d warnings)\n",
			  idle_chk_errors, idle_chk_warnings);
	}
	return idle_chk_errors;
}
