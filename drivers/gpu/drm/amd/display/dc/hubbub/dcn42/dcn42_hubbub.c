// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "dcn30/dcn30_hubbub.h"
#include "dcn31/dcn31_hubbub.h"
#include "dcn32/dcn32_hubbub.h"
#include "dcn35/dcn35_hubbub.h"
#include "dcn42/dcn42_hubbub.h"
#include "dm_services.h"
#include "reg_helper.h"

#define DCN42_CRB_SEGMENT_SIZE_KB 64

#define CTX \
	hubbub2->base.ctx
#define DC_LOGGER \
	hubbub2->base.ctx->logger
#define REG(reg)\
	hubbub2->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	hubbub2->shifts->field_name, hubbub2->masks->field_name

static bool hubbub42_program_urgent_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	bool wm_pending = false;

	/* Repeat for water mark set A, B, C and D. */
	/* clock state A */
	if (safe_to_lower || watermarks->dcn4x.a.urgent > hubbub2->watermarks.dcn4x.a.urgent) {
		hubbub2->watermarks.dcn4x.a.urgent = watermarks->dcn4x.a.urgent;
		REG_SET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, watermarks->dcn4x.a.urgent);
	} else if (watermarks->dcn4x.a.urgent < hubbub2->watermarks.dcn4x.a.urgent)
		wm_pending = true;

	/* determine the transfer time for a quantity of data for a particular requestor.*/
	if (safe_to_lower || watermarks->dcn4x.a.frac_urg_bw_flip > hubbub2->watermarks.dcn4x.a.frac_urg_bw_flip) {
		hubbub2->watermarks.dcn4x.a.frac_urg_bw_flip = watermarks->dcn4x.a.frac_urg_bw_flip;
		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_A, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_FLIP_A, watermarks->dcn4x.a.frac_urg_bw_flip);
	} else if (watermarks->dcn4x.a.frac_urg_bw_flip < hubbub2->watermarks.dcn4x.a.frac_urg_bw_flip)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.a.frac_urg_bw_nom > hubbub2->watermarks.dcn4x.a.frac_urg_bw_nom) {
		hubbub2->watermarks.dcn4x.a.frac_urg_bw_nom = watermarks->dcn4x.a.frac_urg_bw_nom;
		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_NOM_A, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_NOM_A, watermarks->dcn4x.a.frac_urg_bw_nom);
	} else if (watermarks->dcn4x.a.frac_urg_bw_nom < hubbub2->watermarks.dcn4x.a.frac_urg_bw_nom)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.a.refcyc_per_trip_to_mem > hubbub2->watermarks.dcn4x.a.refcyc_per_trip_to_mem) {
		hubbub2->watermarks.dcn4x.a.refcyc_per_trip_to_mem = watermarks->dcn4x.a.refcyc_per_trip_to_mem;
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_A, 0,
				DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_A, watermarks->dcn4x.a.refcyc_per_trip_to_mem);
	} else if (watermarks->dcn4x.a.refcyc_per_trip_to_mem < hubbub2->watermarks.dcn4x.a.refcyc_per_trip_to_mem)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->dcn4x.b.urgent > hubbub2->watermarks.dcn4x.b.urgent) {
		hubbub2->watermarks.dcn4x.b.urgent = watermarks->dcn4x.b.urgent;
		REG_SET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, watermarks->dcn4x.b.urgent);
	} else if (watermarks->dcn4x.b.urgent < hubbub2->watermarks.dcn4x.b.urgent)
		wm_pending = true;

	/* determine the transfer time for a quantity of data for a particular requestor.*/
	if (safe_to_lower || watermarks->dcn4x.b.frac_urg_bw_flip > hubbub2->watermarks.dcn4x.b.frac_urg_bw_flip) {
		hubbub2->watermarks.dcn4x.b.frac_urg_bw_flip = watermarks->dcn4x.b.frac_urg_bw_flip;
		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_B, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_FLIP_B, watermarks->dcn4x.b.frac_urg_bw_flip);
	} else if (watermarks->dcn4x.b.frac_urg_bw_flip < hubbub2->watermarks.dcn4x.b.frac_urg_bw_flip)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.b.frac_urg_bw_nom > hubbub2->watermarks.dcn4x.b.frac_urg_bw_nom) {
		hubbub2->watermarks.dcn4x.b.frac_urg_bw_nom = watermarks->dcn4x.b.frac_urg_bw_nom;
		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_NOM_B, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_NOM_B, watermarks->dcn4x.b.frac_urg_bw_nom);
	} else if (watermarks->dcn4x.b.frac_urg_bw_nom < hubbub2->watermarks.dcn4x.b.frac_urg_bw_nom)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.b.refcyc_per_trip_to_mem > hubbub2->watermarks.dcn4x.b.refcyc_per_trip_to_mem) {
		hubbub2->watermarks.dcn4x.b.refcyc_per_trip_to_mem = watermarks->dcn4x.b.refcyc_per_trip_to_mem;
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_B, 0,
				DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_B, watermarks->dcn4x.b.refcyc_per_trip_to_mem);
	} else if (watermarks->dcn4x.b.refcyc_per_trip_to_mem < hubbub2->watermarks.dcn4x.b.refcyc_per_trip_to_mem)
		wm_pending = true;

	/* clock state C */
	if (safe_to_lower || watermarks->dcn4x.c.urgent > hubbub2->watermarks.dcn4x.c.urgent) {
		hubbub2->watermarks.dcn4x.c.urgent = watermarks->dcn4x.c.urgent;
		REG_SET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C, watermarks->dcn4x.c.urgent);
	} else if (watermarks->dcn4x.c.urgent < hubbub2->watermarks.dcn4x.c.urgent)
		wm_pending = true;

	/* determine the transfer time for a quantity of data for a particular requestor.*/
	if (safe_to_lower || watermarks->dcn4x.c.frac_urg_bw_flip > hubbub2->watermarks.dcn4x.c.frac_urg_bw_flip) {
		hubbub2->watermarks.dcn4x.c.frac_urg_bw_flip = watermarks->dcn4x.c.frac_urg_bw_flip;
		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_C, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_FLIP_C, watermarks->dcn4x.c.frac_urg_bw_flip);
	} else if (watermarks->dcn4x.c.frac_urg_bw_flip < hubbub2->watermarks.dcn4x.c.frac_urg_bw_flip)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.c.frac_urg_bw_nom > hubbub2->watermarks.dcn4x.c.frac_urg_bw_nom) {
		hubbub2->watermarks.dcn4x.c.frac_urg_bw_nom = watermarks->dcn4x.c.frac_urg_bw_nom;
		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_NOM_C, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_NOM_C, watermarks->dcn4x.c.frac_urg_bw_nom);
	} else if (watermarks->dcn4x.c.frac_urg_bw_nom < hubbub2->watermarks.dcn4x.c.frac_urg_bw_nom)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.c.refcyc_per_trip_to_mem > hubbub2->watermarks.dcn4x.c.refcyc_per_trip_to_mem) {
		hubbub2->watermarks.dcn4x.c.refcyc_per_trip_to_mem = watermarks->dcn4x.c.refcyc_per_trip_to_mem;
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_C, 0,
				DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_C, watermarks->dcn4x.c.refcyc_per_trip_to_mem);
	} else if (watermarks->dcn4x.c.refcyc_per_trip_to_mem < hubbub2->watermarks.dcn4x.c.refcyc_per_trip_to_mem)
		wm_pending = true;

	/* clock state D */
	if (safe_to_lower || watermarks->dcn4x.d.urgent > hubbub2->watermarks.dcn4x.d.urgent) {
		hubbub2->watermarks.dcn4x.d.urgent = watermarks->dcn4x.d.urgent;
		REG_SET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D, watermarks->dcn4x.d.urgent);
	} else if (watermarks->dcn4x.d.urgent < hubbub2->watermarks.dcn4x.d.urgent)
		wm_pending = true;

	/* determine the transfer time for a quantity of data for a particular requestor.*/
	if (safe_to_lower || watermarks->dcn4x.d.frac_urg_bw_flip > hubbub2->watermarks.dcn4x.d.frac_urg_bw_flip) {
		hubbub2->watermarks.dcn4x.d.frac_urg_bw_flip = watermarks->dcn4x.d.frac_urg_bw_flip;
		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_D, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_FLIP_D, watermarks->dcn4x.d.frac_urg_bw_flip);
	} else if (watermarks->dcn4x.d.frac_urg_bw_flip < hubbub2->watermarks.dcn4x.d.frac_urg_bw_flip)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.d.frac_urg_bw_nom > hubbub2->watermarks.dcn4x.d.frac_urg_bw_nom) {
		hubbub2->watermarks.dcn4x.d.frac_urg_bw_nom = watermarks->dcn4x.d.frac_urg_bw_nom;
		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_NOM_D, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_NOM_D, watermarks->dcn4x.d.frac_urg_bw_nom);
	} else if (watermarks->dcn4x.d.frac_urg_bw_nom < hubbub2->watermarks.dcn4x.d.frac_urg_bw_nom)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.d.refcyc_per_trip_to_mem > hubbub2->watermarks.dcn4x.d.refcyc_per_trip_to_mem) {
		hubbub2->watermarks.dcn4x.d.refcyc_per_trip_to_mem = watermarks->dcn4x.d.refcyc_per_trip_to_mem;
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_D, 0,
				DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_D, watermarks->dcn4x.d.refcyc_per_trip_to_mem);
	} else if (watermarks->dcn4x.d.refcyc_per_trip_to_mem < hubbub2->watermarks.dcn4x.d.refcyc_per_trip_to_mem)
		wm_pending = true;

	return wm_pending;
}

static bool hubbub42_program_stutter_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	bool wm_pending = false;

	/* clock state A */
	if (safe_to_lower || watermarks->dcn4x.a.sr_enter > hubbub2->watermarks.dcn4x.a.sr_enter) {
		hubbub2->watermarks.dcn4x.a.sr_enter =	watermarks->dcn4x.a.sr_enter;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A, watermarks->dcn4x.a.sr_enter);
	} else if (watermarks->dcn4x.a.sr_enter < hubbub2->watermarks.dcn4x.a.sr_enter)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.a.sr_exit > hubbub2->watermarks.dcn4x.a.sr_exit) {
		hubbub2->watermarks.dcn4x.a.sr_exit = watermarks->dcn4x.a.sr_exit;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A, watermarks->dcn4x.a.sr_exit);
	} else if (watermarks->dcn4x.a.sr_exit < hubbub2->watermarks.dcn4x.a.sr_exit)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->dcn4x.b.sr_enter > hubbub2->watermarks.dcn4x.b.sr_enter) {
		hubbub2->watermarks.dcn4x.b.sr_enter =	watermarks->dcn4x.b.sr_enter;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, watermarks->dcn4x.b.sr_enter);
	} else if (watermarks->dcn4x.b.sr_enter < hubbub2->watermarks.dcn4x.b.sr_enter)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.b.sr_exit > hubbub2->watermarks.dcn4x.b.sr_exit) {
		hubbub2->watermarks.dcn4x.b.sr_exit = watermarks->dcn4x.b.sr_exit;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, watermarks->dcn4x.b.sr_exit);
	} else if (watermarks->dcn4x.b.sr_exit < hubbub2->watermarks.dcn4x.b.sr_exit)
		wm_pending = true;

	/* clock state C */
	if (safe_to_lower || watermarks->dcn4x.c.sr_enter > hubbub2->watermarks.dcn4x.c.sr_enter) {
		hubbub2->watermarks.dcn4x.c.sr_enter =	watermarks->dcn4x.c.sr_enter;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C, watermarks->dcn4x.c.sr_enter);
	} else if (watermarks->dcn4x.c.sr_enter < hubbub2->watermarks.dcn4x.c.sr_enter)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.c.sr_exit > hubbub2->watermarks.dcn4x.c.sr_exit) {
		hubbub2->watermarks.dcn4x.c.sr_exit = watermarks->dcn4x.c.sr_exit;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C, watermarks->dcn4x.c.sr_exit);
	} else if (watermarks->dcn4x.c.sr_exit < hubbub2->watermarks.dcn4x.c.sr_exit)
		wm_pending = true;

	/* clock state D */
	if (safe_to_lower || watermarks->dcn4x.d.sr_enter > hubbub2->watermarks.dcn4x.d.sr_enter) {
		hubbub2->watermarks.dcn4x.d.sr_enter =	watermarks->dcn4x.d.sr_enter;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D, watermarks->dcn4x.d.sr_enter);
	} else if (watermarks->dcn4x.d.sr_enter < hubbub2->watermarks.dcn4x.d.sr_enter)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.d.sr_exit > hubbub2->watermarks.dcn4x.d.sr_exit) {
		hubbub2->watermarks.dcn4x.d.sr_exit = watermarks->dcn4x.d.sr_exit;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D, watermarks->dcn4x.d.sr_exit);
	} else if (watermarks->dcn4x.d.sr_exit < hubbub2->watermarks.dcn4x.d.sr_exit)
		wm_pending = true;

	return wm_pending;
}

static bool hubbub42_program_pstate_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	bool wm_pending = false;

	/* Section for UCLK_PSTATE_CHANGE_WATERMARKS */
	/* UCLK state A */
	if (safe_to_lower || watermarks->dcn4x.a.uclk_pstate > hubbub2->watermarks.dcn4x.a.uclk_pstate) {
		hubbub2->watermarks.dcn4x.a.uclk_pstate = watermarks->dcn4x.a.uclk_pstate;
		REG_SET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_A, 0,
				DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_A, watermarks->dcn4x.a.uclk_pstate);
	} else if (watermarks->dcn4x.a.uclk_pstate < hubbub2->watermarks.dcn4x.a.uclk_pstate)
		wm_pending = true;

	/* UCLK state B */
	if (safe_to_lower || watermarks->dcn4x.b.uclk_pstate > hubbub2->watermarks.dcn4x.b.uclk_pstate) {
		hubbub2->watermarks.dcn4x.b.uclk_pstate = watermarks->dcn4x.b.uclk_pstate;
		REG_SET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_B, 0,
				DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_B, watermarks->dcn4x.b.uclk_pstate);
	} else if (watermarks->dcn4x.b.uclk_pstate < hubbub2->watermarks.dcn4x.b.uclk_pstate)
		wm_pending = true;

	/* UCLK state C */
	if (safe_to_lower || watermarks->dcn4x.c.uclk_pstate > hubbub2->watermarks.dcn4x.c.uclk_pstate) {
		hubbub2->watermarks.dcn4x.c.uclk_pstate = watermarks->dcn4x.c.uclk_pstate;
		REG_SET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_C, 0,
				DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_C, watermarks->dcn4x.c.uclk_pstate);
	} else if (watermarks->dcn4x.c.uclk_pstate < hubbub2->watermarks.dcn4x.c.uclk_pstate)
		wm_pending = true;

	/* UCLK state D */
	if (safe_to_lower || watermarks->dcn4x.d.uclk_pstate > hubbub2->watermarks.dcn4x.d.uclk_pstate) {
		hubbub2->watermarks.dcn4x.d.uclk_pstate = watermarks->dcn4x.d.uclk_pstate;
		REG_SET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_D, 0,
				DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_D, watermarks->dcn4x.d.uclk_pstate);
	} else if (watermarks->dcn4x.d.uclk_pstate < hubbub2->watermarks.dcn4x.d.uclk_pstate)
		wm_pending = true;

	/* Section for FCLK_PSTATE_CHANGE_WATERMARKS */
	/* FCLK state A */
	if (safe_to_lower || watermarks->dcn4x.a.fclk_pstate > hubbub2->watermarks.dcn4x.a.fclk_pstate) {
		hubbub2->watermarks.dcn4x.a.fclk_pstate = watermarks->dcn4x.a.fclk_pstate;
		REG_SET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_A, 0,
				DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_A, watermarks->dcn4x.a.fclk_pstate);
	} else if (watermarks->dcn4x.a.fclk_pstate < hubbub2->watermarks.dcn4x.a.fclk_pstate)
		wm_pending = true;

	/* FCLK state B */
	if (safe_to_lower || watermarks->dcn4x.b.fclk_pstate > hubbub2->watermarks.dcn4x.b.fclk_pstate) {
		hubbub2->watermarks.dcn4x.b.fclk_pstate = watermarks->dcn4x.b.fclk_pstate;
		REG_SET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_B, 0,
				DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_B, watermarks->dcn4x.b.fclk_pstate);
	} else if (watermarks->dcn4x.b.fclk_pstate < hubbub2->watermarks.dcn4x.b.fclk_pstate)
		wm_pending = true;

	/* FCLK state C */
	if (safe_to_lower || watermarks->dcn4x.c.fclk_pstate > hubbub2->watermarks.dcn4x.c.fclk_pstate) {
		hubbub2->watermarks.dcn4x.c.fclk_pstate = watermarks->dcn4x.c.fclk_pstate;
		REG_SET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_C, 0,
				DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_C, watermarks->dcn4x.c.fclk_pstate);
	} else if (watermarks->dcn4x.c.fclk_pstate < hubbub2->watermarks.dcn4x.c.fclk_pstate)
		wm_pending = true;

	/* FCLK state D */
	if (safe_to_lower || watermarks->dcn4x.d.fclk_pstate > hubbub2->watermarks.dcn4x.d.fclk_pstate) {
		hubbub2->watermarks.dcn4x.d.fclk_pstate = watermarks->dcn4x.d.fclk_pstate;
		REG_SET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_D, 0,
				DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_D, watermarks->dcn4x.d.fclk_pstate);
	} else if (watermarks->dcn4x.d.fclk_pstate < hubbub2->watermarks.dcn4x.d.fclk_pstate)
		wm_pending = true;

	return wm_pending;
}

static bool hubbub42_program_usr_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	bool wm_pending = false;

	/* clock state A */
	if (safe_to_lower || watermarks->dcn4x.a.usr > hubbub2->watermarks.dcn4x.a.usr) {
		hubbub2->watermarks.dcn4x.a.usr = watermarks->dcn4x.a.usr;
		REG_SET(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_A, 0,
				DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_A, watermarks->dcn4x.a.usr);
	} else if (watermarks->dcn4x.a.usr < hubbub2->watermarks.dcn4x.a.usr)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->dcn4x.b.usr > hubbub2->watermarks.dcn4x.b.usr) {
		hubbub2->watermarks.dcn4x.b.usr = watermarks->dcn4x.b.usr;
		REG_SET(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_B, 0,
				DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_B, watermarks->dcn4x.b.usr);
	} else if (watermarks->dcn4x.b.usr < hubbub2->watermarks.dcn4x.b.usr)
		wm_pending = true;

	/* clock state C */
	if (safe_to_lower || watermarks->dcn4x.c.usr > hubbub2->watermarks.dcn4x.c.usr) {
		hubbub2->watermarks.dcn4x.c.usr = watermarks->dcn4x.c.usr;
		REG_SET(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_C, 0,
				DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_C, watermarks->dcn4x.c.usr);
	} else if (watermarks->dcn4x.c.usr < hubbub2->watermarks.dcn4x.c.usr)
		wm_pending = true;

	/* clock state D */
	if (safe_to_lower || watermarks->dcn4x.d.usr > hubbub2->watermarks.dcn4x.d.usr) {
		hubbub2->watermarks.dcn4x.d.usr = watermarks->dcn4x.d.usr;
		REG_SET(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_D, 0,
				DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_D, watermarks->dcn4x.d.usr);
	} else if (watermarks->dcn4x.d.usr < hubbub2->watermarks.dcn4x.d.usr)
		wm_pending = true;

	return wm_pending;
}

static bool hubbub42_program_stutter_z8_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	bool wm_pending = false;

	/* clock state A */
	if (safe_to_lower || watermarks->dcn4x.a.sr_enter_z8 > hubbub2->watermarks.dcn4x.a.sr_enter_z8) {
		hubbub2->watermarks.dcn4x.a.sr_enter_z8 = watermarks->dcn4x.a.sr_enter_z8;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_A, watermarks->dcn4x.a.sr_enter_z8);
	} else if (watermarks->dcn4x.a.sr_enter_z8 < hubbub2->watermarks.dcn4x.a.sr_enter_z8)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.a.sr_exit_z8 > hubbub2->watermarks.dcn4x.a.sr_exit_z8) {
		hubbub2->watermarks.dcn4x.a.sr_exit_z8 = watermarks->dcn4x.a.sr_exit_z8;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_A, watermarks->dcn4x.a.sr_exit_z8);
	} else if (watermarks->dcn4x.a.sr_exit_z8 < hubbub2->watermarks.dcn4x.a.sr_exit_z8)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->dcn4x.b.sr_enter_z8 > hubbub2->watermarks.dcn4x.b.sr_enter_z8) {
		hubbub2->watermarks.dcn4x.b.sr_enter_z8 = watermarks->dcn4x.b.sr_enter_z8;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_B, watermarks->dcn4x.b.sr_enter_z8);
	} else if (watermarks->dcn4x.b.sr_enter_z8 < hubbub2->watermarks.dcn4x.b.sr_enter_z8)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.b.sr_exit_z8 > hubbub2->watermarks.dcn4x.b.sr_exit_z8) {
		hubbub2->watermarks.dcn4x.b.sr_exit_z8 = watermarks->dcn4x.b.sr_exit_z8;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_B, watermarks->dcn4x.b.sr_exit_z8);
	} else if (watermarks->dcn4x.b.sr_exit_z8 < hubbub2->watermarks.dcn4x.b.sr_exit_z8)
		wm_pending = true;

	/* clock state C */
	if (safe_to_lower || watermarks->dcn4x.c.sr_enter_z8 > hubbub2->watermarks.dcn4x.c.sr_enter_z8) {
		hubbub2->watermarks.dcn4x.c.sr_enter_z8 = watermarks->dcn4x.c.sr_enter_z8;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_C, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_C, watermarks->dcn4x.c.sr_enter_z8);
	} else if (watermarks->dcn4x.c.sr_enter_z8 < hubbub2->watermarks.dcn4x.c.sr_enter_z8)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.c.sr_exit_z8 > hubbub2->watermarks.dcn4x.c.sr_exit_z8) {
		hubbub2->watermarks.dcn4x.c.sr_exit_z8 = watermarks->dcn4x.c.sr_exit_z8;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_C, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_C, watermarks->dcn4x.c.sr_exit_z8);
	} else if (watermarks->dcn4x.c.sr_exit_z8 < hubbub2->watermarks.dcn4x.c.sr_exit_z8)
		wm_pending = true;

	/* clock state D */
	if (safe_to_lower || watermarks->dcn4x.d.sr_enter_z8 > hubbub2->watermarks.dcn4x.d.sr_enter_z8) {
		hubbub2->watermarks.dcn4x.d.sr_enter_z8 = watermarks->dcn4x.d.sr_enter_z8;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_D, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_D, watermarks->dcn4x.d.sr_enter_z8);
	} else if (watermarks->dcn4x.d.sr_enter_z8 < hubbub2->watermarks.dcn4x.d.sr_enter_z8)
		wm_pending = true;

	if (safe_to_lower || watermarks->dcn4x.d.sr_exit_z8 > hubbub2->watermarks.dcn4x.d.sr_exit_z8) {
		hubbub2->watermarks.dcn4x.d.sr_exit_z8 = watermarks->dcn4x.d.sr_exit_z8;
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_D, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_D, watermarks->dcn4x.d.sr_exit_z8);
	} else if (watermarks->dcn4x.d.sr_exit_z8 < hubbub2->watermarks.dcn4x.d.sr_exit_z8)
		wm_pending = true;

	return wm_pending;
}

static void hubbub42_allow_self_refresh_control(struct hubbub *hubbub, bool allow)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	/*
	 * DCHUBBUB_ARB_ALLOW_SELF_REFRESH_FORCE_ENABLE = 1 means do not allow stutter
	 * DCHUBBUB_ARB_ALLOW_SELF_REFRESH_FORCE_ENABLE = 0 means allow stutter
	 */

	REG_UPDATE_2(DCHUBBUB_ARB_DRAM_STATE_CNTL,
			DCHUBBUB_ARB_ALLOW_SELF_REFRESH_FORCE_VALUE, 0,
			DCHUBBUB_ARB_ALLOW_SELF_REFRESH_FORCE_ENABLE, !allow);

	if (!allow && hubbub->ctx->dc->debug.disable_stutter) {/*controlled by registry key*/
		REG_UPDATE_2(DCHUBBUB_ARB_DRAM_STATE_CNTL,
			DCHUBBUB_ARB_ALLOW_DCFCLK_DEEP_SLEEP_FORCE_VALUE, 0,
			DCHUBBUB_ARB_ALLOW_DCFCLK_DEEP_SLEEP_FORCE_ENABLE, 1);
		REG_UPDATE_2(DCHUBBUB_ARB_DRAM_STATE_CNTL,
			DCHUBBUB_ARB_ALLOW_PSTATE_CHANGE_FORCE_VALUE, 0,
			DCHUBBUB_ARB_ALLOW_PSTATE_CHANGE_FORCE_ENABLE, 1);
	}
}
static void hubbub42_set_sdp_control(struct hubbub *hubbub, bool dc_control)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	REG_UPDATE(DCHUBBUB_SDPIF_CFG0,
			SDPIF_PORT_CONTROL, dc_control);
}

static bool hubbub42_program_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	bool wm_pending = false;
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	if (!safe_to_lower && hubbub->ctx->dc->debug.disable_stutter_for_wm_program) {
		/* before raising watermarks, SDP control give to DF, stutter must be disabled */
		wm_pending = true;
		hubbub42_set_sdp_control(hubbub, false);
		hubbub42_allow_self_refresh_control(hubbub, false);
	}
	if (hubbub42_program_urgent_watermarks(hubbub, watermarks, safe_to_lower))
		wm_pending = true;

	if (hubbub42_program_stutter_watermarks(hubbub, watermarks, safe_to_lower))
		wm_pending = true;

	if (hubbub42_program_pstate_watermarks(hubbub, watermarks, safe_to_lower))
		wm_pending = true;

	if (hubbub42_program_usr_watermarks(hubbub, watermarks, safe_to_lower))
		wm_pending = true;

	if (hubbub42_program_stutter_z8_watermarks(hubbub, watermarks, safe_to_lower))
		wm_pending = true;

	REG_SET(DCHUBBUB_ARB_SAT_LEVEL, 0,
			DCHUBBUB_ARB_SAT_LEVEL, 60 * refclk_mhz);
	REG_UPDATE_2(DCHUBBUB_ARB_DF_REQ_OUTSTAND,
			DCHUBBUB_ARB_MIN_REQ_OUTSTAND, 0xFF,
			DCHUBBUB_ARB_MIN_REQ_OUTSTAND_COMMIT_THRESHOLD, 0xA);/*hw delta*/
	REG_UPDATE(DCHUBBUB_ARB_HOSTVM_CNTL, DCHUBBUB_ARB_MAX_QOS_COMMIT_THRESHOLD, 0xF);

	if (safe_to_lower || hubbub->ctx->dc->debug.disable_stutter)
		hubbub42_allow_self_refresh_control(hubbub, !hubbub->ctx->dc->debug.disable_stutter);
	if (safe_to_lower && hubbub->ctx->dc->debug.disable_stutter_for_wm_program) {
		hubbub42_set_sdp_control(hubbub, true);
	}
	hubbub32_force_usr_retraining_allow(hubbub, hubbub->ctx->dc->debug.force_usr_allow);

	return wm_pending;
}

static const struct hubbub_funcs hubbub42_funcs = {
	.update_dchub = hubbub2_update_dchub,
	.init_dchub_sys_ctx = hubbub31_init_dchub_sys_ctx,
	.init_vm_ctx = hubbub2_init_vm_ctx,
	.dcc_support_swizzle = hubbub3_dcc_support_swizzle,
	.dcc_support_pixel_format = hubbub2_dcc_support_pixel_format,
	.get_dcc_compression_cap = hubbub3_get_dcc_compression_cap,
	.wm_read_state = hubbub35_wm_read_state,
	.get_dchub_ref_freq = hubbub35_get_dchub_ref_freq,
	.program_watermarks = hubbub42_program_watermarks,
	.allow_self_refresh_control = hubbub42_allow_self_refresh_control,
	.is_allow_self_refresh_enabled = hubbub1_is_allow_self_refresh_enabled,
	.force_wm_propagate_to_pipes = hubbub32_force_wm_propagate_to_pipes,
	.force_pstate_change_control = hubbub3_force_pstate_change_control,
	.init_watermarks = hubbub35_init_watermarks,
	.program_det_size = dcn32_program_det_size,
	.program_compbuf_size = dcn35_program_compbuf_size,
	.init_crb = dcn35_init_crb,
	.hubbub_read_state = hubbub2_read_state,
	.force_usr_retraining_allow = hubbub32_force_usr_retraining_allow,
	.dchubbub_init = hubbub35_init,
	.dchvm_init = dcn35_dchvm_init,
};

void hubbub42_construct(struct dcn20_hubbub *hubbub2,
	struct dc_context *ctx,
	const struct dcn_hubbub_registers *hubbub_regs,
	const struct dcn_hubbub_shift *hubbub_shift,
	const struct dcn_hubbub_mask *hubbub_mask,
	int det_size_kb,
	int pixel_chunk_size_kb,
	int config_return_buffer_size_kb)
{
	hubbub2->base.ctx = ctx;
	hubbub2->base.funcs = &hubbub42_funcs;
	hubbub2->regs = hubbub_regs;
	hubbub2->shifts = hubbub_shift;
	hubbub2->masks = hubbub_mask;

	hubbub2->detile_buf_size = det_size_kb * 1024;
	hubbub2->pixel_chunk_size = pixel_chunk_size_kb * 1024;
	hubbub2->crb_size_segs = config_return_buffer_size_kb / DCN42_CRB_SEGMENT_SIZE_KB;
}
