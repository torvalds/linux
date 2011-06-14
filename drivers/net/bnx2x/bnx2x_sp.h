#ifndef BNX2X_SP
#define BNX2X_SP

#include "bnx2x_reg.h"

/* MAC configuration */
void bnx2x_set_mac_addr_gen(struct bnx2x *bp, int set, const u8 *mac,
			    u32 cl_bit_vec, u8 cam_offset,
			    u8 is_bcast);

/* Multicast */
void bnx2x_invalidate_e1_mc_list(struct bnx2x *bp);
void bnx2x_invalidate_e1h_mc_list(struct bnx2x *bp);
int bnx2x_set_e1_mc_list(struct bnx2x *bp);
int bnx2x_set_e1h_mc_list(struct bnx2x *bp);

/* Rx mode */
void bnx2x_set_storm_rx_mode(struct bnx2x *bp);
void bnx2x_rxq_set_mac_filters(struct bnx2x *bp, u16 cl_id, u32 filters);

/* RSS configuration */
void bnx2x_func_init(struct bnx2x *bp, struct bnx2x_func_init_params *p);
void bnx2x_push_indir_table(struct bnx2x *bp);

/* Queue configuration */
static inline void bnx2x_set_ctx_validation(struct eth_context *cxt, u32 cid)
{
	/* ustorm cxt validation */
	cxt->ustorm_ag_context.cdu_usage =
		CDU_RSRVD_VALUE_TYPE_A(cid, CDU_REGION_NUMBER_UCM_AG,
				       ETH_CONNECTION_TYPE);
	/* xcontext validation */
	cxt->xstorm_ag_context.cdu_reserved =
		CDU_RSRVD_VALUE_TYPE_A(cid, CDU_REGION_NUMBER_XCM_AG,
				       ETH_CONNECTION_TYPE);
}

int bnx2x_setup_fw_client(struct bnx2x *bp,
			  struct bnx2x_client_init_params *params,
			  u8 activate,
			  struct client_init_ramrod_data *data,
			  dma_addr_t data_mapping);
#endif /* BNX2X_SP */
