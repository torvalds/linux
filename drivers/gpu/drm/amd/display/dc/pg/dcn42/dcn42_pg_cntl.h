/* SPDX-License-Identifier: MIT */

/* Copyright 2026 Advanced Micro Devices, Inc.*/

#ifndef __DCN42_PG_CNTL_H__
#define __DCN42_PG_CNTL_H__

#include "pg_cntl.h"

#define PG_CNTL_REG_LIST_DCN42()\
	SR(DOMAIN0_PG_CONFIG), \
	SR(DOMAIN1_PG_CONFIG), \
	SR(DOMAIN2_PG_CONFIG), \
	SR(DOMAIN3_PG_CONFIG), \
	SR(DOMAIN16_PG_CONFIG), \
	SR(DOMAIN17_PG_CONFIG), \
	SR(DOMAIN18_PG_CONFIG), \
	SR(DOMAIN19_PG_CONFIG), \
	SR(DOMAIN22_PG_CONFIG), \
	SR(DOMAIN23_PG_CONFIG), \
	SR(DOMAIN24_PG_CONFIG), \
	SR(DOMAIN25_PG_CONFIG), \
	SR(DOMAIN26_PG_CONFIG), \
	SR(DOMAIN0_PG_STATUS), \
	SR(DOMAIN1_PG_STATUS), \
	SR(DOMAIN2_PG_STATUS), \
	SR(DOMAIN3_PG_STATUS), \
	SR(DOMAIN16_PG_STATUS), \
	SR(DOMAIN17_PG_STATUS), \
	SR(DOMAIN18_PG_STATUS), \
	SR(DOMAIN19_PG_STATUS), \
	SR(DOMAIN22_PG_STATUS), \
	SR(DOMAIN23_PG_STATUS), \
	SR(DOMAIN24_PG_STATUS), \
	SR(DOMAIN25_PG_STATUS), \
	SR(DOMAIN26_PG_STATUS), \
	SR(DC_IP_REQUEST_CNTL)

#define PG_CNTL_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define PG_CNTL_MASK_SH_LIST_DCN42(mask_sh) \
	PG_CNTL_SF(DOMAIN0_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	PG_CNTL_SF(DOMAIN0_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	PG_CNTL_SF(DOMAIN1_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	PG_CNTL_SF(DOMAIN1_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	PG_CNTL_SF(DOMAIN2_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	PG_CNTL_SF(DOMAIN2_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	PG_CNTL_SF(DOMAIN3_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	PG_CNTL_SF(DOMAIN3_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	PG_CNTL_SF(DOMAIN16_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	PG_CNTL_SF(DOMAIN16_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	PG_CNTL_SF(DOMAIN17_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	PG_CNTL_SF(DOMAIN17_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	PG_CNTL_SF(DOMAIN18_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	PG_CNTL_SF(DOMAIN18_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	PG_CNTL_SF(DOMAIN19_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	PG_CNTL_SF(DOMAIN19_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	PG_CNTL_SF(DOMAIN22_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	PG_CNTL_SF(DOMAIN22_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	PG_CNTL_SF(DOMAIN23_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	PG_CNTL_SF(DOMAIN23_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	PG_CNTL_SF(DOMAIN24_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	PG_CNTL_SF(DOMAIN24_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	PG_CNTL_SF(DOMAIN25_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	PG_CNTL_SF(DOMAIN25_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	PG_CNTL_SF(DOMAIN26_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	PG_CNTL_SF(DOMAIN26_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	PG_CNTL_SF(DOMAIN0_PG_STATUS, DOMAIN_DESIRED_PWR_STATE, mask_sh), \
	PG_CNTL_SF(DOMAIN0_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	PG_CNTL_SF(DOMAIN1_PG_STATUS, DOMAIN_DESIRED_PWR_STATE, mask_sh), \
	PG_CNTL_SF(DOMAIN1_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	PG_CNTL_SF(DOMAIN2_PG_STATUS, DOMAIN_DESIRED_PWR_STATE, mask_sh), \
	PG_CNTL_SF(DOMAIN2_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	PG_CNTL_SF(DOMAIN3_PG_STATUS, DOMAIN_DESIRED_PWR_STATE, mask_sh), \
	PG_CNTL_SF(DOMAIN3_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	PG_CNTL_SF(DOMAIN16_PG_STATUS, DOMAIN_DESIRED_PWR_STATE, mask_sh), \
	PG_CNTL_SF(DOMAIN16_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	PG_CNTL_SF(DOMAIN17_PG_STATUS, DOMAIN_DESIRED_PWR_STATE, mask_sh), \
	PG_CNTL_SF(DOMAIN17_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	PG_CNTL_SF(DOMAIN18_PG_STATUS, DOMAIN_DESIRED_PWR_STATE, mask_sh), \
	PG_CNTL_SF(DOMAIN18_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	PG_CNTL_SF(DOMAIN19_PG_STATUS, DOMAIN_DESIRED_PWR_STATE, mask_sh), \
	PG_CNTL_SF(DOMAIN19_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	PG_CNTL_SF(DOMAIN22_PG_STATUS, DOMAIN_DESIRED_PWR_STATE, mask_sh), \
	PG_CNTL_SF(DOMAIN22_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	PG_CNTL_SF(DOMAIN23_PG_STATUS, DOMAIN_DESIRED_PWR_STATE, mask_sh), \
	PG_CNTL_SF(DOMAIN23_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	PG_CNTL_SF(DOMAIN24_PG_STATUS, DOMAIN_DESIRED_PWR_STATE, mask_sh), \
	PG_CNTL_SF(DOMAIN24_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	PG_CNTL_SF(DOMAIN25_PG_STATUS, DOMAIN_DESIRED_PWR_STATE, mask_sh), \
	PG_CNTL_SF(DOMAIN25_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	PG_CNTL_SF(DOMAIN26_PG_STATUS, DOMAIN_DESIRED_PWR_STATE, mask_sh), \
	PG_CNTL_SF(DOMAIN26_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	PG_CNTL_SF(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, mask_sh)

struct pg_cntl_shift {
	uint8_t IP_REQUEST_EN;
	uint8_t DOMAIN_POWER_FORCEON;
	uint8_t DOMAIN_POWER_GATE;
	uint8_t DOMAIN_DESIRED_PWR_STATE;
	uint8_t DOMAIN_PGFSM_PWR_STATUS;
};
struct pg_cntl_mask {
	uint32_t IP_REQUEST_EN;
	uint32_t DOMAIN_POWER_FORCEON;
	uint32_t DOMAIN_POWER_GATE;
	uint32_t DOMAIN_DESIRED_PWR_STATE;
	uint32_t DOMAIN_PGFSM_PWR_STATUS;
};

struct pg_cntl_registers {
	uint32_t LONO_STATE;
	uint32_t DC_IP_REQUEST_CNTL;
	uint32_t DOMAIN0_PG_CONFIG;
	uint32_t DOMAIN1_PG_CONFIG;
	uint32_t DOMAIN2_PG_CONFIG;
	uint32_t DOMAIN3_PG_CONFIG;
	uint32_t DOMAIN16_PG_CONFIG;
	uint32_t DOMAIN17_PG_CONFIG;
	uint32_t DOMAIN18_PG_CONFIG;
	uint32_t DOMAIN19_PG_CONFIG;
	uint32_t DOMAIN22_PG_CONFIG;
	uint32_t DOMAIN23_PG_CONFIG;
	uint32_t DOMAIN24_PG_CONFIG;
	uint32_t DOMAIN25_PG_CONFIG;
	uint32_t DOMAIN26_PG_CONFIG;
	uint32_t DOMAIN0_PG_STATUS;
	uint32_t DOMAIN1_PG_STATUS;
	uint32_t DOMAIN2_PG_STATUS;
	uint32_t DOMAIN3_PG_STATUS;
	uint32_t DOMAIN16_PG_STATUS;
	uint32_t DOMAIN17_PG_STATUS;
	uint32_t DOMAIN18_PG_STATUS;
	uint32_t DOMAIN19_PG_STATUS;
	uint32_t DOMAIN22_PG_STATUS;
	uint32_t DOMAIN23_PG_STATUS;
	uint32_t DOMAIN24_PG_STATUS;
	uint32_t DOMAIN25_PG_STATUS;
	uint32_t DOMAIN26_PG_STATUS;
};

struct dcn_pg_cntl {
	struct pg_cntl base;
	const struct pg_cntl_registers *regs;
	const struct pg_cntl_shift *pg_cntl_shift;
	const struct pg_cntl_mask *pg_cntl_mask;
};

void pg_cntl42_dsc_pg_control(struct pg_cntl *pg_cntl, unsigned int dsc_inst, bool power_on);
void pg_cntl42_hubp_dpp_pg_control(struct pg_cntl *pg_cntl,
	unsigned int hubp_dpp_inst, bool power_on);
void pg_cntl42_hpo_pg_control(struct pg_cntl *pg_cntl, bool power_on);
void pg_cntl42_io_clk_pg_control(struct pg_cntl *pg_cntl, bool power_on);
void pg_cntl42_plane_otg_pg_control(struct pg_cntl *pg_cntl, bool power_on);
void pg_cntl42_mpcc_pg_control(struct pg_cntl *pg_cntl,
	unsigned int mpcc_inst, bool power_on);
void pg_cntl42_opp_pg_control(struct pg_cntl *pg_cntl,
	unsigned int opp_inst, bool power_on);
void pg_cntl42_optc_pg_control(struct pg_cntl *pg_cntl,
	unsigned int optc_inst, bool power_on);
void pg_cntl42_mem_pg_control(struct pg_cntl *pg_cntl, bool power_on);
void pg_cntl42_dio_pg_control(struct pg_cntl *pg_cntl, bool power_on);
void dcn42_pg_cntl_destroy(struct pg_cntl **pg_cntl);
void pg_cntl42_init_pg_status(struct pg_cntl *pg_cntl);

struct pg_cntl *pg_cntl42_create(
	struct dc_context *ctx,
	const struct pg_cntl_registers *regs,
	const struct pg_cntl_shift *pg_cntl_shift,
	const struct pg_cntl_mask *pg_cntl_mask);

void dcn_pg_cntl_destroy(struct pg_cntl **pg_cntl);

#endif /* DCN42_PG_CNTL */
