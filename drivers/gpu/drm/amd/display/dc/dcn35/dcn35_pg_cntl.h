/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef _DCN35_PG_CNTL_H_
#define _DCN35_PG_CNTL_H_

#include "pg_cntl.h"

#define PG_CNTL_REG_LIST_DCN35()\
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
	SR(DC_IP_REQUEST_CNTL)

#define PG_CNTL_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define PG_CNTL_MASK_SH_LIST_DCN35(mask_sh) \
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
	PG_CNTL_SF(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, mask_sh)

#define PG_CNTL_REG_FIELD_LIST(type) \
	type IPS2;\
	type IPS1;\
	type IPS0;\
	type IPS0_All

#define PG_CNTL_DCN35_REG_FIELD_LIST(type) \
	type IP_REQUEST_EN; \
	type DOMAIN_POWER_FORCEON; \
	type DOMAIN_POWER_GATE; \
	type DOMAIN_DESIRED_PWR_STATE; \
	type DOMAIN_PGFSM_PWR_STATUS

struct pg_cntl_shift {
	PG_CNTL_REG_FIELD_LIST(uint8_t);
	PG_CNTL_DCN35_REG_FIELD_LIST(uint8_t);
};

struct pg_cntl_mask {
	PG_CNTL_REG_FIELD_LIST(uint32_t);
	PG_CNTL_DCN35_REG_FIELD_LIST(uint32_t);
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
};

struct dcn_pg_cntl {
	struct pg_cntl base;
	const struct pg_cntl_registers *regs;
	const struct pg_cntl_shift *pg_cntl_shift;
	const struct pg_cntl_mask *pg_cntl_mask;
};

void pg_cntl35_dsc_pg_control(struct pg_cntl *pg_cntl, unsigned int dsc_inst, bool power_on);
void pg_cntl35_hubp_dpp_pg_control(struct pg_cntl *pg_cntl,
	unsigned int hubp_dpp_inst, bool power_on);
void pg_cntl35_hpo_pg_control(struct pg_cntl *pg_cntl, bool power_on);
void pg_cntl35_io_clk_pg_control(struct pg_cntl *pg_cntl, bool power_on);
void pg_cntl35_plane_otg_pg_control(struct pg_cntl *pg_cntl, bool power_on);
void pg_cntl35_mpcc_pg_control(struct pg_cntl *pg_cntl,
	unsigned int mpcc_inst, bool power_on);
void pg_cntl35_opp_pg_control(struct pg_cntl *pg_cntl,
	unsigned int opp_inst, bool power_on);
void pg_cntl35_optc_pg_control(struct pg_cntl *pg_cntl,
	unsigned int optc_inst, bool power_on);
void pg_cntl35_dwb_pg_control(struct pg_cntl *pg_cntl, bool power_on);
void pg_cntl35_init_pg_status(struct pg_cntl *pg_cntl);
void pg_cntl35_set_force_poweron_domain22(struct pg_cntl *pg_cntl, bool power_on);

struct pg_cntl *pg_cntl35_create(
	struct dc_context *ctx,
	const struct pg_cntl_registers *regs,
	const struct pg_cntl_shift *pg_cntl_shift,
	const struct pg_cntl_mask *pg_cntl_mask);

void dcn_pg_cntl_destroy(struct pg_cntl **pg_cntl);

#endif /* DCN35_PG_CNTL */
