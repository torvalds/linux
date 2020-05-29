// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "system_global.h"

#ifdef USE_INPUT_SYSTEM_VERSION_2

#include "input_system.h"
#include <type_support.h>
#include "gp_device.h"

#include "assert_support.h"

#ifndef __INLINE_INPUT_SYSTEM__
#include "input_system_private.h"
#endif /* __INLINE_INPUT_SYSTEM__ */

#define ZERO (0x0)
#define ONE  (1U)

static const ib_buffer_t   IB_BUFFER_NULL = {0, 0, 0 };

static input_system_error_t input_system_configure_channel(
    const channel_cfg_t		channel);

static input_system_error_t input_system_configure_channel_sensor(
    const channel_cfg_t		channel);

static input_system_error_t input_buffer_configuration(void);

static input_system_error_t configuration_to_registers(void);

static void receiver_rst(const rx_ID_t ID);
static void input_system_network_rst(const input_system_ID_t ID);

static void capture_unit_configure(
    const input_system_ID_t			ID,
    const sub_system_ID_t			sub_id,
    const ib_buffer_t *const cfg);

static void acquisition_unit_configure(
    const input_system_ID_t			ID,
    const sub_system_ID_t			sub_id,
    const ib_buffer_t *const cfg);

static void ctrl_unit_configure(
    const input_system_ID_t			ID,
    const sub_system_ID_t			sub_id,
    const ctrl_unit_cfg_t *const cfg);

static void input_system_network_configure(
    const input_system_ID_t			ID,
    const input_system_network_cfg_t *const cfg);

// MW: CSI is previously named as "rx" short for "receiver"
static input_system_error_t set_csi_cfg(
    csi_cfg_t *const lhs,
    const csi_cfg_t *const rhs,
    input_system_config_flags_t *const flags);

static input_system_error_t set_source_type(
    input_system_source_t *const lhs,
    const input_system_source_t				rhs,
    input_system_config_flags_t *const flags);

static input_system_error_t input_system_multiplexer_cfg(
    input_system_multiplex_t *const lhs,
    const input_system_multiplex_t			rhs,
    input_system_config_flags_t *const flags);

static inline void capture_unit_get_state(
    const input_system_ID_t			ID,
    const sub_system_ID_t			sub_id,
    capture_unit_state_t			*state);

static inline void acquisition_unit_get_state(
    const input_system_ID_t			ID,
    const sub_system_ID_t			sub_id,
    acquisition_unit_state_t		*state);

static inline void ctrl_unit_get_state(
    const input_system_ID_t			ID,
    const sub_system_ID_t			sub_id,
    ctrl_unit_state_t				*state);

static inline void mipi_port_get_state(
    const rx_ID_t					ID,
    const enum mipi_port_id			port_ID,
    mipi_port_state_t				*state);

static inline void rx_channel_get_state(
    const rx_ID_t					ID,
    const unsigned int				ch_id,
    rx_channel_state_t				*state);

static void gp_device_rst(const gp_device_ID_t		ID);

static void input_selector_cfg_for_sensor(const gp_device_ID_t	ID);

static void input_switch_rst(const gp_device_ID_t	ID);

static void input_switch_cfg(
    const gp_device_ID_t				ID,
    const input_switch_cfg_t *const cfg
);

void input_system_get_state(
    const input_system_ID_t			ID,
    input_system_state_t			*state)
{
	sub_system_ID_t	sub_id;

	assert(ID < N_INPUT_SYSTEM_ID);
	assert(state);

	state->str_multicastA_sel = input_system_sub_system_reg_load(ID,
				    GPREGS_UNIT0_ID,
				    HIVE_ISYS_GPREG_MULTICAST_A_IDX);
	state->str_multicastB_sel = input_system_sub_system_reg_load(ID,
				    GPREGS_UNIT0_ID,
				    HIVE_ISYS_GPREG_MULTICAST_B_IDX);
	state->str_multicastC_sel = input_system_sub_system_reg_load(ID,
				    GPREGS_UNIT0_ID,
				    HIVE_ISYS_GPREG_MULTICAST_C_IDX);
	state->str_mux_sel = input_system_sub_system_reg_load(ID,
			     GPREGS_UNIT0_ID,
			     HIVE_ISYS_GPREG_MUX_IDX);
	state->str_mon_status = input_system_sub_system_reg_load(ID,
				GPREGS_UNIT0_ID,
				HIVE_ISYS_GPREG_STRMON_STAT_IDX);
	state->str_mon_irq_cond = input_system_sub_system_reg_load(ID,
				  GPREGS_UNIT0_ID,
				  HIVE_ISYS_GPREG_STRMON_COND_IDX);
	state->str_mon_irq_en = input_system_sub_system_reg_load(ID,
				GPREGS_UNIT0_ID,
				HIVE_ISYS_GPREG_STRMON_IRQ_EN_IDX);
	state->isys_srst = input_system_sub_system_reg_load(ID,
			   GPREGS_UNIT0_ID,
			   HIVE_ISYS_GPREG_SRST_IDX);
	state->isys_slv_reg_srst = input_system_sub_system_reg_load(ID,
				   GPREGS_UNIT0_ID,
				   HIVE_ISYS_GPREG_SLV_REG_SRST_IDX);
	state->str_deint_portA_cnt = input_system_sub_system_reg_load(ID,
				     GPREGS_UNIT0_ID,
				     HIVE_ISYS_GPREG_REG_PORT_A_IDX);
	state->str_deint_portB_cnt = input_system_sub_system_reg_load(ID,
				     GPREGS_UNIT0_ID,
				     HIVE_ISYS_GPREG_REG_PORT_B_IDX);

	for (sub_id = CAPTURE_UNIT0_ID; sub_id < CAPTURE_UNIT0_ID + N_CAPTURE_UNIT_ID;
	     sub_id++) {
		capture_unit_get_state(ID, sub_id,
				       &state->capture_unit[sub_id - CAPTURE_UNIT0_ID]);
	}
	for (sub_id = ACQUISITION_UNIT0_ID;
	     sub_id < ACQUISITION_UNIT0_ID + N_ACQUISITION_UNIT_ID; sub_id++) {
		acquisition_unit_get_state(ID, sub_id,
					   &state->acquisition_unit[sub_id - ACQUISITION_UNIT0_ID]);
	}
	for (sub_id = CTRL_UNIT0_ID; sub_id < CTRL_UNIT0_ID + N_CTRL_UNIT_ID;
	     sub_id++) {
		ctrl_unit_get_state(ID, sub_id,
				    &state->ctrl_unit_state[sub_id - CTRL_UNIT0_ID]);
	}

	return;
}

void receiver_get_state(
    const rx_ID_t				ID,
    receiver_state_t			*state)
{
	enum mipi_port_id	port_id;
	unsigned int	ch_id;

	assert(ID < N_RX_ID);
	assert(state);

	state->fs_to_ls_delay = (uint8_t)receiver_reg_load(ID,
				_HRT_CSS_RECEIVER_FS_TO_LS_DELAY_REG_IDX);
	state->ls_to_data_delay = (uint8_t)receiver_reg_load(ID,
				  _HRT_CSS_RECEIVER_LS_TO_DATA_DELAY_REG_IDX);
	state->data_to_le_delay = (uint8_t)receiver_reg_load(ID,
				  _HRT_CSS_RECEIVER_DATA_TO_LE_DELAY_REG_IDX);
	state->le_to_fe_delay = (uint8_t)receiver_reg_load(ID,
				_HRT_CSS_RECEIVER_LE_TO_FE_DELAY_REG_IDX);
	state->fe_to_fs_delay = (uint8_t)receiver_reg_load(ID,
				_HRT_CSS_RECEIVER_FE_TO_FS_DELAY_REG_IDX);
	state->le_to_fs_delay = (uint8_t)receiver_reg_load(ID,
				_HRT_CSS_RECEIVER_LE_TO_LS_DELAY_REG_IDX);
	state->is_two_ppc = (bool)receiver_reg_load(ID,
			    _HRT_CSS_RECEIVER_TWO_PIXEL_EN_REG_IDX);
	state->backend_rst = receiver_reg_load(ID,
					       _HRT_CSS_RECEIVER_BACKEND_RST_REG_IDX);
	state->raw18 = (uint16_t)receiver_reg_load(ID,
		       _HRT_CSS_RECEIVER_RAW18_REG_IDX);
	state->force_raw8 = (bool)receiver_reg_load(ID,
			    _HRT_CSS_RECEIVER_FORCE_RAW8_REG_IDX);
	state->raw16 = (uint16_t)receiver_reg_load(ID,
		       _HRT_CSS_RECEIVER_RAW16_REG_IDX);

	for (port_id = (enum mipi_port_id)0; port_id < N_MIPI_PORT_ID; port_id++) {
		mipi_port_get_state(ID, port_id,
				    &state->mipi_port_state[port_id]);
	}
	for (ch_id = 0U; ch_id < N_RX_CHANNEL_ID; ch_id++) {
		rx_channel_get_state(ID, ch_id,
				     &state->rx_channel_state[ch_id]);
	}

	state->be_gsp_acc_ovl = receiver_reg_load(ID,
				_HRT_CSS_RECEIVER_BE_GSP_ACC_OVL_REG_IDX);
	state->be_srst = receiver_reg_load(ID,
					   _HRT_CSS_RECEIVER_BE_SRST_REG_IDX);
	state->be_is_two_ppc = receiver_reg_load(ID,
			       _HRT_CSS_RECEIVER_BE_TWO_PPC_REG_IDX);
	state->be_comp_format0 = receiver_reg_load(ID,
				 _HRT_CSS_RECEIVER_BE_COMP_FORMAT_REG0_IDX);
	state->be_comp_format1 = receiver_reg_load(ID,
				 _HRT_CSS_RECEIVER_BE_COMP_FORMAT_REG1_IDX);
	state->be_comp_format2 = receiver_reg_load(ID,
				 _HRT_CSS_RECEIVER_BE_COMP_FORMAT_REG2_IDX);
	state->be_comp_format3 = receiver_reg_load(ID,
				 _HRT_CSS_RECEIVER_BE_COMP_FORMAT_REG3_IDX);
	state->be_sel = receiver_reg_load(ID,
					  _HRT_CSS_RECEIVER_BE_SEL_REG_IDX);
	state->be_raw16_config = receiver_reg_load(ID,
				 _HRT_CSS_RECEIVER_BE_RAW16_CONFIG_REG_IDX);
	state->be_raw18_config = receiver_reg_load(ID,
				 _HRT_CSS_RECEIVER_BE_RAW18_CONFIG_REG_IDX);
	state->be_force_raw8 = receiver_reg_load(ID,
			       _HRT_CSS_RECEIVER_BE_FORCE_RAW8_REG_IDX);
	state->be_irq_status = receiver_reg_load(ID,
			       _HRT_CSS_RECEIVER_BE_IRQ_STATUS_REG_IDX);
	state->be_irq_clear = receiver_reg_load(ID,
						_HRT_CSS_RECEIVER_BE_IRQ_CLEAR_REG_IDX);

	return;
}

bool is_mipi_format_yuv420(
    const mipi_format_t			mipi_format)
{
	bool	is_yuv420 = (
				(mipi_format == MIPI_FORMAT_YUV420_8) ||
				(mipi_format == MIPI_FORMAT_YUV420_10) ||
				(mipi_format == MIPI_FORMAT_YUV420_8_SHIFT) ||
				(mipi_format == MIPI_FORMAT_YUV420_10_SHIFT));
	/* MIPI_FORMAT_YUV420_8_LEGACY is not YUV420 */

	return is_yuv420;
}

void receiver_set_compression(
    const rx_ID_t			ID,
    const unsigned int		cfg_ID,
    const mipi_compressor_t		comp,
    const mipi_predictor_t		pred)
{
	const unsigned int		field_id = cfg_ID % N_MIPI_FORMAT_CUSTOM;
	const unsigned int		ch_id = cfg_ID / N_MIPI_FORMAT_CUSTOM;
	hrt_data			val;
	hrt_address			addr = 0;
	hrt_data			reg;

	assert(ID < N_RX_ID);
	assert(cfg_ID < N_MIPI_COMPRESSOR_CONTEXT);
	assert(field_id < N_MIPI_FORMAT_CUSTOM);
	assert(ch_id < N_RX_CHANNEL_ID);
	assert(comp < N_MIPI_COMPRESSOR_METHODS);
	assert(pred < N_MIPI_PREDICTOR_TYPES);

	val = (((uint8_t)pred) << 3) | comp;

	switch (ch_id) {
	case 0:
		addr = ((field_id < 6) ? _HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC0_REG0_IDX :
			_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC0_REG1_IDX);
		break;
	case 1:
		addr = ((field_id < 6) ? _HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC1_REG0_IDX :
			_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC1_REG1_IDX);
		break;
	case 2:
		addr = ((field_id < 6) ? _HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC2_REG0_IDX :
			_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC2_REG1_IDX);
		break;
	case 3:
		addr = ((field_id < 6) ? _HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC3_REG0_IDX :
			_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC3_REG1_IDX);
		break;
	default:
		/* should not happen */
		assert(false);
		return;
	}

	reg = ((field_id < 6) ? (val << (field_id * 5)) : (val << ((
		    field_id - 6) * 5)));
	receiver_reg_store(ID, addr, reg);

	return;
}

void receiver_port_enable(
    const rx_ID_t			ID,
    const enum mipi_port_id		port_ID,
    const bool			cnd)
{
	hrt_data	reg = receiver_port_reg_load(ID, port_ID,
			  _HRT_CSS_RECEIVER_DEVICE_READY_REG_IDX);

	if (cnd) {
		reg |= 0x01;
	} else {
		reg &= ~0x01;
	}

	receiver_port_reg_store(ID, port_ID,
				_HRT_CSS_RECEIVER_DEVICE_READY_REG_IDX, reg);
	return;
}

bool is_receiver_port_enabled(
    const rx_ID_t			ID,
    const enum mipi_port_id		port_ID)
{
	hrt_data	reg = receiver_port_reg_load(ID, port_ID,
			  _HRT_CSS_RECEIVER_DEVICE_READY_REG_IDX);
	return ((reg & 0x01) != 0);
}

void receiver_irq_enable(
    const rx_ID_t			ID,
    const enum mipi_port_id		port_ID,
    const rx_irq_info_t		irq_info)
{
	receiver_port_reg_store(ID,
				port_ID, _HRT_CSS_RECEIVER_IRQ_ENABLE_REG_IDX, irq_info);
	return;
}

rx_irq_info_t receiver_get_irq_info(
    const rx_ID_t			ID,
    const enum mipi_port_id		port_ID)
{
	return receiver_port_reg_load(ID,
				      port_ID, _HRT_CSS_RECEIVER_IRQ_STATUS_REG_IDX);
}

void receiver_irq_clear(
    const rx_ID_t			ID,
    const enum mipi_port_id		port_ID,
    const rx_irq_info_t		irq_info)
{
	receiver_port_reg_store(ID,
				port_ID, _HRT_CSS_RECEIVER_IRQ_STATUS_REG_IDX, irq_info);
	return;
}

static inline void capture_unit_get_state(
    const input_system_ID_t			ID,
    const sub_system_ID_t			sub_id,
    capture_unit_state_t			*state)
{
	assert(/*(sub_id >= CAPTURE_UNIT0_ID) &&*/ (sub_id <= CAPTURE_UNIT2_ID));
	assert(state);

	state->StartMode = input_system_sub_system_reg_load(ID,
			   sub_id,
			   CAPT_START_MODE_REG_ID);
	state->Start_Addr = input_system_sub_system_reg_load(ID,
			    sub_id,
			    CAPT_START_ADDR_REG_ID);
	state->Mem_Region_Size = input_system_sub_system_reg_load(ID,
				 sub_id,
				 CAPT_MEM_REGION_SIZE_REG_ID);
	state->Num_Mem_Regions = input_system_sub_system_reg_load(ID,
				 sub_id,
				 CAPT_NUM_MEM_REGIONS_REG_ID);
//	AM: Illegal read from following registers.
	/*	state->Init = input_system_sub_system_reg_load(ID,
			sub_id,
			CAPT_INIT_REG_ID);
		state->Start = input_system_sub_system_reg_load(ID,
			sub_id,
			CAPT_START_REG_ID);
		state->Stop = input_system_sub_system_reg_load(ID,
			sub_id,
			CAPT_STOP_REG_ID);
	*/
	state->Packet_Length = input_system_sub_system_reg_load(ID,
			       sub_id,
			       CAPT_PACKET_LENGTH_REG_ID);
	state->Received_Length = input_system_sub_system_reg_load(ID,
				 sub_id,
				 CAPT_RECEIVED_LENGTH_REG_ID);
	state->Received_Short_Packets = input_system_sub_system_reg_load(ID,
					sub_id,
					CAPT_RECEIVED_SHORT_PACKETS_REG_ID);
	state->Received_Long_Packets = input_system_sub_system_reg_load(ID,
				       sub_id,
				       CAPT_RECEIVED_LONG_PACKETS_REG_ID);
	state->Last_Command = input_system_sub_system_reg_load(ID,
			      sub_id,
			      CAPT_LAST_COMMAND_REG_ID);
	state->Next_Command = input_system_sub_system_reg_load(ID,
			      sub_id,
			      CAPT_NEXT_COMMAND_REG_ID);
	state->Last_Acknowledge = input_system_sub_system_reg_load(ID,
				  sub_id,
				  CAPT_LAST_ACKNOWLEDGE_REG_ID);
	state->Next_Acknowledge = input_system_sub_system_reg_load(ID,
				  sub_id,
				  CAPT_NEXT_ACKNOWLEDGE_REG_ID);
	state->FSM_State_Info = input_system_sub_system_reg_load(ID,
				sub_id,
				CAPT_FSM_STATE_INFO_REG_ID);

	return;
}

static inline void acquisition_unit_get_state(
    const input_system_ID_t			ID,
    const sub_system_ID_t			sub_id,
    acquisition_unit_state_t		*state)
{
	assert(sub_id == ACQUISITION_UNIT0_ID);
	assert(state);

	state->Start_Addr = input_system_sub_system_reg_load(ID,
			    sub_id,
			    ACQ_START_ADDR_REG_ID);
	state->Mem_Region_Size = input_system_sub_system_reg_load(ID,
				 sub_id,
				 ACQ_MEM_REGION_SIZE_REG_ID);
	state->Num_Mem_Regions = input_system_sub_system_reg_load(ID,
				 sub_id,
				 ACQ_NUM_MEM_REGIONS_REG_ID);
//	AM: Illegal read from following registers.
	/*	state->Init = input_system_sub_system_reg_load(ID,
			sub_id,
			ACQ_INIT_REG_ID);
	*/
	state->Received_Short_Packets = input_system_sub_system_reg_load(ID,
					sub_id,
					ACQ_RECEIVED_SHORT_PACKETS_REG_ID);
	state->Received_Long_Packets = input_system_sub_system_reg_load(ID,
				       sub_id,
				       ACQ_RECEIVED_LONG_PACKETS_REG_ID);
	state->Last_Command = input_system_sub_system_reg_load(ID,
			      sub_id,
			      ACQ_LAST_COMMAND_REG_ID);
	state->Next_Command = input_system_sub_system_reg_load(ID,
			      sub_id,
			      ACQ_NEXT_COMMAND_REG_ID);
	state->Last_Acknowledge = input_system_sub_system_reg_load(ID,
				  sub_id,
				  ACQ_LAST_ACKNOWLEDGE_REG_ID);
	state->Next_Acknowledge = input_system_sub_system_reg_load(ID,
				  sub_id,
				  ACQ_NEXT_ACKNOWLEDGE_REG_ID);
	state->FSM_State_Info = input_system_sub_system_reg_load(ID,
				sub_id,
				ACQ_FSM_STATE_INFO_REG_ID);
	state->Int_Cntr_Info = input_system_sub_system_reg_load(ID,
			       sub_id,
			       ACQ_INT_CNTR_INFO_REG_ID);

	return;
}

static inline void ctrl_unit_get_state(
    const input_system_ID_t			ID,
    const sub_system_ID_t			sub_id,
    ctrl_unit_state_t			*state)
{
	assert(sub_id == CTRL_UNIT0_ID);
	assert(state);

	state->captA_start_addr = input_system_sub_system_reg_load(ID,
				  sub_id,
				  ISYS_CTRL_CAPT_START_ADDR_A_REG_ID);
	state->captB_start_addr = input_system_sub_system_reg_load(ID,
				  sub_id,
				  ISYS_CTRL_CAPT_START_ADDR_B_REG_ID);
	state->captC_start_addr = input_system_sub_system_reg_load(ID,
				  sub_id,
				  ISYS_CTRL_CAPT_START_ADDR_C_REG_ID);
	state->captA_mem_region_size = input_system_sub_system_reg_load(ID,
				       sub_id,
				       ISYS_CTRL_CAPT_MEM_REGION_SIZE_A_REG_ID);
	state->captB_mem_region_size = input_system_sub_system_reg_load(ID,
				       sub_id,
				       ISYS_CTRL_CAPT_MEM_REGION_SIZE_B_REG_ID);
	state->captC_mem_region_size = input_system_sub_system_reg_load(ID,
				       sub_id,
				       ISYS_CTRL_CAPT_MEM_REGION_SIZE_C_REG_ID);
	state->captA_num_mem_regions = input_system_sub_system_reg_load(ID,
				       sub_id,
				       ISYS_CTRL_CAPT_NUM_MEM_REGIONS_A_REG_ID);
	state->captB_num_mem_regions = input_system_sub_system_reg_load(ID,
				       sub_id,
				       ISYS_CTRL_CAPT_NUM_MEM_REGIONS_B_REG_ID);
	state->captC_num_mem_regions = input_system_sub_system_reg_load(ID,
				       sub_id,
				       ISYS_CTRL_CAPT_NUM_MEM_REGIONS_C_REG_ID);
	state->acq_start_addr = input_system_sub_system_reg_load(ID,
				sub_id,
				ISYS_CTRL_ACQ_START_ADDR_REG_ID);
	state->acq_mem_region_size = input_system_sub_system_reg_load(ID,
				     sub_id,
				     ISYS_CTRL_ACQ_MEM_REGION_SIZE_REG_ID);
	state->acq_num_mem_regions = input_system_sub_system_reg_load(ID,
				     sub_id,
				     ISYS_CTRL_ACQ_NUM_MEM_REGIONS_REG_ID);
//	AM: Illegal read from following registers.
	/*	state->ctrl_init = input_system_sub_system_reg_load(ID,
			sub_id,
			ISYS_CTRL_INIT_REG_ID);
	*/
	state->last_cmd = input_system_sub_system_reg_load(ID,
			  sub_id,
			  ISYS_CTRL_LAST_COMMAND_REG_ID);
	state->next_cmd = input_system_sub_system_reg_load(ID,
			  sub_id,
			  ISYS_CTRL_NEXT_COMMAND_REG_ID);
	state->last_ack = input_system_sub_system_reg_load(ID,
			  sub_id,
			  ISYS_CTRL_LAST_ACKNOWLEDGE_REG_ID);
	state->next_ack = input_system_sub_system_reg_load(ID,
			  sub_id,
			  ISYS_CTRL_NEXT_ACKNOWLEDGE_REG_ID);
	state->top_fsm_state = input_system_sub_system_reg_load(ID,
			       sub_id,
			       ISYS_CTRL_FSM_STATE_INFO_REG_ID);
	state->captA_fsm_state = input_system_sub_system_reg_load(ID,
				 sub_id,
				 ISYS_CTRL_CAPT_A_FSM_STATE_INFO_REG_ID);
	state->captB_fsm_state = input_system_sub_system_reg_load(ID,
				 sub_id,
				 ISYS_CTRL_CAPT_B_FSM_STATE_INFO_REG_ID);
	state->captC_fsm_state = input_system_sub_system_reg_load(ID,
				 sub_id,
				 ISYS_CTRL_CAPT_C_FSM_STATE_INFO_REG_ID);
	state->acq_fsm_state = input_system_sub_system_reg_load(ID,
			       sub_id,
			       ISYS_CTRL_ACQ_FSM_STATE_INFO_REG_ID);
	state->capt_reserve_one_mem_region = input_system_sub_system_reg_load(ID,
					     sub_id,
					     ISYS_CTRL_CAPT_RESERVE_ONE_MEM_REGION_REG_ID);

	return;
}

static inline void mipi_port_get_state(
    const rx_ID_t				ID,
    const enum mipi_port_id			port_ID,
    mipi_port_state_t			*state)
{
	int	i;

	assert(ID < N_RX_ID);
	assert(port_ID < N_MIPI_PORT_ID);
	assert(state);

	state->device_ready = receiver_port_reg_load(ID,
			      port_ID, _HRT_CSS_RECEIVER_DEVICE_READY_REG_IDX);
	state->irq_status = receiver_port_reg_load(ID,
			    port_ID, _HRT_CSS_RECEIVER_IRQ_STATUS_REG_IDX);
	state->irq_enable = receiver_port_reg_load(ID,
			    port_ID, _HRT_CSS_RECEIVER_IRQ_ENABLE_REG_IDX);
	state->timeout_count = receiver_port_reg_load(ID,
			       port_ID, _HRT_CSS_RECEIVER_TIMEOUT_COUNT_REG_IDX);
	state->init_count = (uint16_t)receiver_port_reg_load(ID,
			    port_ID, _HRT_CSS_RECEIVER_INIT_COUNT_REG_IDX);
	state->raw16_18 = (uint16_t)receiver_port_reg_load(ID,
			  port_ID, _HRT_CSS_RECEIVER_RAW16_18_DATAID_REG_IDX);
	state->sync_count = receiver_port_reg_load(ID,
			    port_ID, _HRT_CSS_RECEIVER_SYNC_COUNT_REG_IDX);
	state->rx_count = receiver_port_reg_load(ID,
			  port_ID, _HRT_CSS_RECEIVER_RX_COUNT_REG_IDX);

	for (i = 0; i < MIPI_4LANE_CFG ; i++) {
		state->lane_sync_count[i] = (uint8_t)((state->sync_count) >> (i * 8));
		state->lane_rx_count[i] = (uint8_t)((state->rx_count) >> (i * 8));
	}

	return;
}

static inline void rx_channel_get_state(
    const rx_ID_t					ID,
    const unsigned int				ch_id,
    rx_channel_state_t				*state)
{
	int	i;

	assert(ID < N_RX_ID);
	assert(ch_id < N_RX_CHANNEL_ID);
	assert(state);

	switch (ch_id) {
	case 0:
		state->comp_scheme0 = receiver_reg_load(ID,
							_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC0_REG0_IDX);
		state->comp_scheme1 = receiver_reg_load(ID,
							_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC0_REG1_IDX);
		break;
	case 1:
		state->comp_scheme0 = receiver_reg_load(ID,
							_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC1_REG0_IDX);
		state->comp_scheme1 = receiver_reg_load(ID,
							_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC1_REG1_IDX);
		break;
	case 2:
		state->comp_scheme0 = receiver_reg_load(ID,
							_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC2_REG0_IDX);
		state->comp_scheme1 = receiver_reg_load(ID,
							_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC2_REG1_IDX);
		break;
	case 3:
		state->comp_scheme0 = receiver_reg_load(ID,
							_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC3_REG0_IDX);
		state->comp_scheme1 = receiver_reg_load(ID,
							_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC3_REG1_IDX);
		break;
	}

	/* See Table 7.1.17,..., 7.1.24 */
	for (i = 0; i < 6; i++) {
		u8	val = (uint8_t)((state->comp_scheme0) >> (i * 5)) & 0x1f;

		state->comp[i] = (mipi_compressor_t)(val & 0x07);
		state->pred[i] = (mipi_predictor_t)((val & 0x18) >> 3);
	}
	for (i = 6; i < N_MIPI_FORMAT_CUSTOM; i++) {
		u8	val = (uint8_t)((state->comp_scheme0) >> ((i - 6) * 5)) & 0x1f;

		state->comp[i] = (mipi_compressor_t)(val & 0x07);
		state->pred[i] = (mipi_predictor_t)((val & 0x18) >> 3);
	}

	return;
}

// MW: "2400" in the name is not good, but this is to avoid a naming conflict
static input_system_cfg2400_t config;

static void receiver_rst(
    const rx_ID_t				ID)
{
	enum mipi_port_id		port_id;

	assert(ID < N_RX_ID);

// Disable all ports.
	for (port_id = MIPI_PORT0_ID; port_id < N_MIPI_PORT_ID; port_id++) {
		receiver_port_enable(ID, port_id, false);
	}

	// AM: Additional actions for stopping receiver?

	return;
}

//Single function to reset all the devices mapped via GP_DEVICE.
static void gp_device_rst(const gp_device_ID_t		ID)
{
	assert(ID < N_GP_DEVICE_ID);

	gp_device_reg_store(ID, _REG_GP_SYNCGEN_ENABLE_ADDR, ZERO);
	// gp_device_reg_store(ID, _REG_GP_SYNCGEN_FREE_RUNNING_ADDR, ZERO);
	// gp_device_reg_store(ID, _REG_GP_SYNCGEN_PAUSE_ADDR, ONE);
	// gp_device_reg_store(ID, _REG_GP_NR_FRAMES_ADDR, ZERO);
	// gp_device_reg_store(ID, _REG_GP_SYNGEN_NR_PIX_ADDR, ZERO);
	// gp_device_reg_store(ID, _REG_GP_SYNGEN_NR_PIX_ADDR, ZERO);
	// gp_device_reg_store(ID, _REG_GP_SYNGEN_NR_LINES_ADDR, ZERO);
	// gp_device_reg_store(ID, _REG_GP_SYNGEN_HBLANK_CYCLES_ADDR, ZERO);
	// gp_device_reg_store(ID, _REG_GP_SYNGEN_VBLANK_CYCLES_ADDR, ZERO);
// AM: Following calls cause strange warnings. Probably they should not be initialized.
//	gp_device_reg_store(ID, _REG_GP_ISEL_SOF_ADDR, ZERO);
//	gp_device_reg_store(ID, _REG_GP_ISEL_EOF_ADDR, ZERO);
//	gp_device_reg_store(ID, _REG_GP_ISEL_SOL_ADDR, ZERO);
//	gp_device_reg_store(ID, _REG_GP_ISEL_EOL_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_LFSR_ENABLE_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_LFSR_ENABLE_B_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_LFSR_RESET_VALUE_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_TPG_ENABLE_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_TPG_ENABLE_B_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_HOR_CNT_MASK_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_VER_CNT_MASK_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_XY_CNT_MASK_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_HOR_CNT_DELTA_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_VER_CNT_DELTA_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_TPG_MODE_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_TPG_RED1_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_TPG_GREEN1_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_TPG_BLUE1_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_TPG_RED2_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_TPG_GREEN2_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_TPG_BLUE2_ADDR, ZERO);
	//gp_device_reg_store(ID, _REG_GP_ISEL_CH_ID_ADDR, ZERO);
	//gp_device_reg_store(ID, _REG_GP_ISEL_FMT_TYPE_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_DATA_SEL_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_SBAND_SEL_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_SYNC_SEL_ADDR, ZERO);
	//	gp_device_reg_store(ID, _REG_GP_SYNCGEN_HOR_CNT_ADDR, ZERO);
	//	gp_device_reg_store(ID, _REG_GP_SYNCGEN_VER_CNT_ADDR, ZERO);
	//	gp_device_reg_store(ID, _REG_GP_SYNCGEN_FRAME_CNT_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_SOFT_RESET_ADDR,
			    ZERO); // AM: Maybe this soft reset is not safe.

	return;
}

static void input_selector_cfg_for_sensor(const gp_device_ID_t ID)
{
	assert(ID < N_GP_DEVICE_ID);

	gp_device_reg_store(ID, _REG_GP_ISEL_SOF_ADDR, ONE);
	gp_device_reg_store(ID, _REG_GP_ISEL_EOF_ADDR, ONE);
	gp_device_reg_store(ID, _REG_GP_ISEL_SOL_ADDR, ONE);
	gp_device_reg_store(ID, _REG_GP_ISEL_EOL_ADDR, ONE);
	gp_device_reg_store(ID, _REG_GP_ISEL_CH_ID_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_FMT_TYPE_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_DATA_SEL_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_SBAND_SEL_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_ISEL_SYNC_SEL_ADDR, ZERO);
	gp_device_reg_store(ID, _REG_GP_SOFT_RESET_ADDR, ZERO);

	return;
}

static void input_switch_rst(const gp_device_ID_t ID)
{
	int addr;

	assert(ID < N_GP_DEVICE_ID);

	// Initialize the data&hsync LUT.
	for (addr = _REG_GP_IFMT_input_switch_lut_reg0;
	     addr <= _REG_GP_IFMT_input_switch_lut_reg7; addr += SIZEOF_HRT_REG) {
		gp_device_reg_store(ID, addr, ZERO);
	}

	// Initialize the vsync LUT.
	gp_device_reg_store(ID,
			    _REG_GP_IFMT_input_switch_fsync_lut,
			    ZERO);

	return;
}

static void input_switch_cfg(
    const gp_device_ID_t			ID,
    const input_switch_cfg_t *const cfg)
{
	int addr_offset;

	assert(ID < N_GP_DEVICE_ID);
	assert(cfg);

	// Initialize the data&hsync LUT.
	for (addr_offset = 0; addr_offset < N_RX_CHANNEL_ID * 2; addr_offset++) {
		assert(addr_offset * SIZEOF_HRT_REG + _REG_GP_IFMT_input_switch_lut_reg0 <=
		       _REG_GP_IFMT_input_switch_lut_reg7);
		gp_device_reg_store(ID,
				    _REG_GP_IFMT_input_switch_lut_reg0 + addr_offset * SIZEOF_HRT_REG,
				    cfg->hsync_data_reg[addr_offset]);
	}

	// Initialize the vsync LUT.
	gp_device_reg_store(ID,
			    _REG_GP_IFMT_input_switch_fsync_lut,
			    cfg->vsync_data_reg);

	return;
}

static void input_system_network_rst(const input_system_ID_t ID)
{
	unsigned int sub_id;

	// Reset all 3 multicasts.
	input_system_sub_system_reg_store(ID,
					  GPREGS_UNIT0_ID,
					  HIVE_ISYS_GPREG_MULTICAST_A_IDX,
					  INPUT_SYSTEM_DISCARD_ALL);
	input_system_sub_system_reg_store(ID,
					  GPREGS_UNIT0_ID,
					  HIVE_ISYS_GPREG_MULTICAST_B_IDX,
					  INPUT_SYSTEM_DISCARD_ALL);
	input_system_sub_system_reg_store(ID,
					  GPREGS_UNIT0_ID,
					  HIVE_ISYS_GPREG_MULTICAST_C_IDX,
					  INPUT_SYSTEM_DISCARD_ALL);

	// Reset stream mux.
	input_system_sub_system_reg_store(ID,
					  GPREGS_UNIT0_ID,
					  HIVE_ISYS_GPREG_MUX_IDX,
					  N_INPUT_SYSTEM_MULTIPLEX);

	// Reset 3 capture units.
	for (sub_id = CAPTURE_UNIT0_ID; sub_id < CAPTURE_UNIT0_ID + N_CAPTURE_UNIT_ID;
	     sub_id++) {
		input_system_sub_system_reg_store(ID,
						  sub_id,
						  CAPT_INIT_REG_ID,
						  1U << CAPT_INIT_RST_REG_BIT);
	}

	// Reset acquisition unit.
	for (sub_id = ACQUISITION_UNIT0_ID;
	     sub_id < ACQUISITION_UNIT0_ID + N_ACQUISITION_UNIT_ID; sub_id++) {
		input_system_sub_system_reg_store(ID,
						  sub_id,
						  ACQ_INIT_REG_ID,
						  1U << ACQ_INIT_RST_REG_BIT);
	}

	// DMA unit reset is not needed.

	// Reset controller units.
	// NB: In future we need to keep part of ctrl_state for split capture and
	for (sub_id = CTRL_UNIT0_ID; sub_id < CTRL_UNIT0_ID + N_CTRL_UNIT_ID;
	     sub_id++) {
		input_system_sub_system_reg_store(ID,
						  sub_id,
						  ISYS_CTRL_INIT_REG_ID,
						  1U); //AM: Is there any named constant?
	}

	return;
}

// Function that resets current configuration.
input_system_error_t input_system_configuration_reset(void)
{
	unsigned int i;

	receiver_rst(RX0_ID);

	input_system_network_rst(INPUT_SYSTEM0_ID);

	gp_device_rst(GP_DEVICE0_ID);

	input_switch_rst(GP_DEVICE0_ID);

	//target_rst();

	// Reset IRQ_CTRLs.

	// Reset configuration data structures.
	for (i = 0; i < N_CHANNELS; i++) {
		config.ch_flags[i] = INPUT_SYSTEM_CFG_FLAG_RESET;
		config.target_isp_flags[i] = INPUT_SYSTEM_CFG_FLAG_RESET;
		config.target_sp_flags[i] = INPUT_SYSTEM_CFG_FLAG_RESET;
		config.target_strm2mem_flags[i] = INPUT_SYSTEM_CFG_FLAG_RESET;
	}

	for (i = 0; i < N_CSI_PORTS; i++) {
		config.csi_buffer_flags[i]	 = INPUT_SYSTEM_CFG_FLAG_RESET;
		config.multicast[i]		 = INPUT_SYSTEM_DISCARD_ALL;
	}

	config.source_type_flags				 = INPUT_SYSTEM_CFG_FLAG_RESET;
	config.acquisition_buffer_unique_flags	 = INPUT_SYSTEM_CFG_FLAG_RESET;
	config.unallocated_ib_mem_words			 = IB_CAPACITY_IN_WORDS;
	//config.acq_allocated_ib_mem_words		 = 0;

	// Set the start of the session cofiguration.
	config.session_flags = INPUT_SYSTEM_CFG_FLAG_REQUIRED;

	return INPUT_SYSTEM_ERR_NO_ERROR;
}

// MW: Comments are good, but doxygen is required, place it at the declaration
// Function that appends the channel to current configuration.
static input_system_error_t input_system_configure_channel(
    const channel_cfg_t		channel)
{
	input_system_error_t error = INPUT_SYSTEM_ERR_NO_ERROR;
	// Check if channel is not already configured.
	if (config.ch_flags[channel.ch_id] & INPUT_SYSTEM_CFG_FLAG_SET) {
		return INPUT_SYSTEM_ERR_CHANNEL_ALREADY_SET;
	} else {
		switch (channel.source_type) {
		case INPUT_SYSTEM_SOURCE_SENSOR:
			error = input_system_configure_channel_sensor(channel);
			break;
		case INPUT_SYSTEM_SOURCE_TPG:
			return INPUT_SYSTEM_ERR_PARAMETER_NOT_SUPPORTED;
			break;
		case INPUT_SYSTEM_SOURCE_PRBS:
			return INPUT_SYSTEM_ERR_PARAMETER_NOT_SUPPORTED;
			break;
		case INPUT_SYSTEM_SOURCE_FIFO:
			return INPUT_SYSTEM_ERR_PARAMETER_NOT_SUPPORTED;
			break;
		default:
			return INPUT_SYSTEM_ERR_PARAMETER_NOT_SUPPORTED;
			break;
		}

		if (error != INPUT_SYSTEM_ERR_NO_ERROR) return error;
		// Input switch channel configurations must be combined in united config.
		config.input_switch_cfg.hsync_data_reg[channel.source_cfg.csi_cfg.csi_port * 2]
		    =
			channel.target_cfg.input_switch_channel_cfg.hsync_data_reg[0];
		config.input_switch_cfg.hsync_data_reg[channel.source_cfg.csi_cfg.csi_port * 2 +
											   1] =
							       channel.target_cfg.input_switch_channel_cfg.hsync_data_reg[1];
		config.input_switch_cfg.vsync_data_reg |=
		    (channel.target_cfg.input_switch_channel_cfg.vsync_data_reg & 0x7) <<
		    (channel.source_cfg.csi_cfg.csi_port * 3);

		// Other targets are just copied and marked as set.
		config.target_isp[channel.source_cfg.csi_cfg.csi_port] =
		    channel.target_cfg.target_isp_cfg;
		config.target_sp[channel.source_cfg.csi_cfg.csi_port] =
		    channel.target_cfg.target_sp_cfg;
		config.target_strm2mem[channel.source_cfg.csi_cfg.csi_port] =
		    channel.target_cfg.target_strm2mem_cfg;
		config.target_isp_flags[channel.source_cfg.csi_cfg.csi_port] |=
		    INPUT_SYSTEM_CFG_FLAG_SET;
		config.target_sp_flags[channel.source_cfg.csi_cfg.csi_port] |=
		    INPUT_SYSTEM_CFG_FLAG_SET;
		config.target_strm2mem_flags[channel.source_cfg.csi_cfg.csi_port] |=
		    INPUT_SYSTEM_CFG_FLAG_SET;

		config.ch_flags[channel.ch_id] = INPUT_SYSTEM_CFG_FLAG_SET;
	}
	return INPUT_SYSTEM_ERR_NO_ERROR;
}

// Function that partitions input buffer space with determining addresses.
static input_system_error_t input_buffer_configuration(void)
{
	u32 current_address    = 0;
	u32 unallocated_memory = IB_CAPACITY_IN_WORDS;

	ib_buffer_t	candidate_buffer_acq  = IB_BUFFER_NULL;
	u32 size_requested;
	input_system_config_flags_t	acq_already_specified = INPUT_SYSTEM_CFG_FLAG_RESET;
	input_system_csi_port_t port;

	for (port = INPUT_SYSTEM_PORT_A; port < N_INPUT_SYSTEM_PORTS; port++) {
		csi_cfg_t source = config.csi_value[port];//.csi_cfg;

		if (config.csi_flags[port] & INPUT_SYSTEM_CFG_FLAG_SET) {
			// Check and set csi buffer in input buffer.
			switch (source.buffering_mode) {
			case INPUT_SYSTEM_FIFO_CAPTURE:
			case INPUT_SYSTEM_XMEM_ACQUIRE:
				config.csi_buffer_flags[port] =
				    INPUT_SYSTEM_CFG_FLAG_BLOCKED; // Well, not used.
				break;

			case INPUT_SYSTEM_FIFO_CAPTURE_WITH_COUNTING:
			case INPUT_SYSTEM_SRAM_BUFFERING:
			case INPUT_SYSTEM_XMEM_BUFFERING:
			case INPUT_SYSTEM_XMEM_CAPTURE:
				size_requested = source.csi_buffer.mem_reg_size *
						 source.csi_buffer.nof_mem_regs;
				if (source.csi_buffer.mem_reg_size > 0
				    && source.csi_buffer.nof_mem_regs > 0
				    && size_requested <= unallocated_memory
				   ) {
					config.csi_buffer[port].mem_reg_addr = current_address;
					config.csi_buffer[port].mem_reg_size = source.csi_buffer.mem_reg_size;
					config.csi_buffer[port].nof_mem_regs = source.csi_buffer.nof_mem_regs;
					current_address		+= size_requested;
					unallocated_memory	-= size_requested;
					config.csi_buffer_flags[port] = INPUT_SYSTEM_CFG_FLAG_SET;
				} else {
					config.csi_buffer_flags[port] |= INPUT_SYSTEM_CFG_FLAG_CONFLICT;
					return INPUT_SYSTEM_ERR_CONFLICT_ON_RESOURCE;
				}
				break;

			default:
				config.csi_buffer_flags[port] |= INPUT_SYSTEM_CFG_FLAG_CONFLICT;
				return INPUT_SYSTEM_ERR_PARAMETER_NOT_SUPPORTED;
				break;
			}

			// Check acquisition buffer specified but set it later since it has to be unique.
			switch (source.buffering_mode) {
			case INPUT_SYSTEM_FIFO_CAPTURE:
			case INPUT_SYSTEM_SRAM_BUFFERING:
			case INPUT_SYSTEM_XMEM_CAPTURE:
				// Nothing to do.
				break;

			case INPUT_SYSTEM_FIFO_CAPTURE_WITH_COUNTING:
			case INPUT_SYSTEM_XMEM_BUFFERING:
			case INPUT_SYSTEM_XMEM_ACQUIRE:
				if (acq_already_specified == INPUT_SYSTEM_CFG_FLAG_RESET) {
					size_requested = source.acquisition_buffer.mem_reg_size
							 * source.acquisition_buffer.nof_mem_regs;
					if (source.acquisition_buffer.mem_reg_size > 0
					    && source.acquisition_buffer.nof_mem_regs > 0
					    && size_requested <= unallocated_memory
					   ) {
						candidate_buffer_acq = source.acquisition_buffer;
						acq_already_specified = INPUT_SYSTEM_CFG_FLAG_SET;
					}
				} else {
					// Check if specified acquisition buffer is the same as specified before.
					if (source.acquisition_buffer.mem_reg_size != candidate_buffer_acq.mem_reg_size
					    || source.acquisition_buffer.nof_mem_regs !=  candidate_buffer_acq.nof_mem_regs
					   ) {
						config.acquisition_buffer_unique_flags |= INPUT_SYSTEM_CFG_FLAG_CONFLICT;
						return INPUT_SYSTEM_ERR_CONFLICT_ON_RESOURCE;
					}
				}
				break;

			default:
				return INPUT_SYSTEM_ERR_PARAMETER_NOT_SUPPORTED;
				break;
			}
		} else {
			config.csi_buffer_flags[port] = INPUT_SYSTEM_CFG_FLAG_BLOCKED;
		}
	} // end of for ( port )

	// Set the acquisition buffer at the end.
	size_requested = candidate_buffer_acq.mem_reg_size *
			 candidate_buffer_acq.nof_mem_regs;
	if (acq_already_specified == INPUT_SYSTEM_CFG_FLAG_SET
	    && size_requested <= unallocated_memory) {
		config.acquisition_buffer_unique.mem_reg_addr = current_address;
		config.acquisition_buffer_unique.mem_reg_size =
		    candidate_buffer_acq.mem_reg_size;
		config.acquisition_buffer_unique.nof_mem_regs =
		    candidate_buffer_acq.nof_mem_regs;
		current_address		+= size_requested;
		unallocated_memory	-= size_requested;
		config.acquisition_buffer_unique_flags = INPUT_SYSTEM_CFG_FLAG_SET;

		assert(current_address <= IB_CAPACITY_IN_WORDS);
	}

	return INPUT_SYSTEM_ERR_NO_ERROR;
}

static void capture_unit_configure(
    const input_system_ID_t			ID,
    const sub_system_ID_t			sub_id,
    const ib_buffer_t *const cfg)
{
	assert(ID < N_INPUT_SYSTEM_ID);
	assert(/*(sub_id >= CAPTURE_UNIT0_ID) &&*/ (sub_id <=
		CAPTURE_UNIT2_ID)); // Commented part is always true.
	assert(cfg);

	input_system_sub_system_reg_store(ID,
					  sub_id,
					  CAPT_START_ADDR_REG_ID,
					  cfg->mem_reg_addr);
	input_system_sub_system_reg_store(ID,
					  sub_id,
					  CAPT_MEM_REGION_SIZE_REG_ID,
					  cfg->mem_reg_size);
	input_system_sub_system_reg_store(ID,
					  sub_id,
					  CAPT_NUM_MEM_REGIONS_REG_ID,
					  cfg->nof_mem_regs);

	return;
}

static void acquisition_unit_configure(
    const input_system_ID_t			ID,
    const sub_system_ID_t			sub_id,
    const ib_buffer_t *const cfg)
{
	assert(ID < N_INPUT_SYSTEM_ID);
	assert(sub_id == ACQUISITION_UNIT0_ID);
	assert(cfg);

	input_system_sub_system_reg_store(ID,
					  sub_id,
					  ACQ_START_ADDR_REG_ID,
					  cfg->mem_reg_addr);
	input_system_sub_system_reg_store(ID,
					  sub_id,
					  ACQ_NUM_MEM_REGIONS_REG_ID,
					  cfg->nof_mem_regs);
	input_system_sub_system_reg_store(ID,
					  sub_id,
					  ACQ_MEM_REGION_SIZE_REG_ID,
					  cfg->mem_reg_size);

	return;
}

static void ctrl_unit_configure(
    const input_system_ID_t			ID,
    const sub_system_ID_t			sub_id,
    const ctrl_unit_cfg_t *const cfg)
{
	assert(ID < N_INPUT_SYSTEM_ID);
	assert(sub_id == CTRL_UNIT0_ID);
	assert(cfg);

	input_system_sub_system_reg_store(ID,
					  sub_id,
					  ISYS_CTRL_CAPT_START_ADDR_A_REG_ID,
					  cfg->buffer_mipi[CAPTURE_UNIT0_ID].mem_reg_addr);
	input_system_sub_system_reg_store(ID,
					  sub_id,
					  ISYS_CTRL_CAPT_MEM_REGION_SIZE_A_REG_ID,
					  cfg->buffer_mipi[CAPTURE_UNIT0_ID].mem_reg_size);
	input_system_sub_system_reg_store(ID,
					  sub_id,
					  ISYS_CTRL_CAPT_NUM_MEM_REGIONS_A_REG_ID,
					  cfg->buffer_mipi[CAPTURE_UNIT0_ID].nof_mem_regs);

	input_system_sub_system_reg_store(ID,
					  sub_id,
					  ISYS_CTRL_CAPT_START_ADDR_B_REG_ID,
					  cfg->buffer_mipi[CAPTURE_UNIT1_ID].mem_reg_addr);
	input_system_sub_system_reg_store(ID,
					  sub_id,
					  ISYS_CTRL_CAPT_MEM_REGION_SIZE_B_REG_ID,
					  cfg->buffer_mipi[CAPTURE_UNIT1_ID].mem_reg_size);
	input_system_sub_system_reg_store(ID,
					  sub_id,
					  ISYS_CTRL_CAPT_NUM_MEM_REGIONS_B_REG_ID,
					  cfg->buffer_mipi[CAPTURE_UNIT1_ID].nof_mem_regs);

	input_system_sub_system_reg_store(ID,
					  sub_id,
					  ISYS_CTRL_CAPT_START_ADDR_C_REG_ID,
					  cfg->buffer_mipi[CAPTURE_UNIT2_ID].mem_reg_addr);
	input_system_sub_system_reg_store(ID,
					  sub_id,
					  ISYS_CTRL_CAPT_MEM_REGION_SIZE_C_REG_ID,
					  cfg->buffer_mipi[CAPTURE_UNIT2_ID].mem_reg_size);
	input_system_sub_system_reg_store(ID,
					  sub_id,
					  ISYS_CTRL_CAPT_NUM_MEM_REGIONS_C_REG_ID,
					  cfg->buffer_mipi[CAPTURE_UNIT2_ID].nof_mem_regs);

	input_system_sub_system_reg_store(ID,
					  sub_id,
					  ISYS_CTRL_ACQ_START_ADDR_REG_ID,
					  cfg->buffer_acquire[ACQUISITION_UNIT0_ID - ACQUISITION_UNIT0_ID].mem_reg_addr);
	input_system_sub_system_reg_store(ID,
					  sub_id,
					  ISYS_CTRL_ACQ_MEM_REGION_SIZE_REG_ID,
					  cfg->buffer_acquire[ACQUISITION_UNIT0_ID - ACQUISITION_UNIT0_ID].mem_reg_size);
	input_system_sub_system_reg_store(ID,
					  sub_id,
					  ISYS_CTRL_ACQ_NUM_MEM_REGIONS_REG_ID,
					  cfg->buffer_acquire[ACQUISITION_UNIT0_ID - ACQUISITION_UNIT0_ID].nof_mem_regs);
	input_system_sub_system_reg_store(ID,
					  sub_id,
					  ISYS_CTRL_CAPT_RESERVE_ONE_MEM_REGION_REG_ID,
					  0);
	return;
}

static void input_system_network_configure(
    const input_system_ID_t				ID,
    const input_system_network_cfg_t *const cfg)
{
	u32 sub_id;

	assert(ID < N_INPUT_SYSTEM_ID);
	assert(cfg);

	// Set all 3 multicasts.
	input_system_sub_system_reg_store(ID,
					  GPREGS_UNIT0_ID,
					  HIVE_ISYS_GPREG_MULTICAST_A_IDX,
					  cfg->multicast_cfg[CAPTURE_UNIT0_ID]);
	input_system_sub_system_reg_store(ID,
					  GPREGS_UNIT0_ID,
					  HIVE_ISYS_GPREG_MULTICAST_B_IDX,
					  cfg->multicast_cfg[CAPTURE_UNIT1_ID]);
	input_system_sub_system_reg_store(ID,
					  GPREGS_UNIT0_ID,
					  HIVE_ISYS_GPREG_MULTICAST_C_IDX,
					  cfg->multicast_cfg[CAPTURE_UNIT2_ID]);

	// Set stream mux.
	input_system_sub_system_reg_store(ID,
					  GPREGS_UNIT0_ID,
					  HIVE_ISYS_GPREG_MUX_IDX,
					  cfg->mux_cfg);

	// Set capture units.
	for (sub_id = CAPTURE_UNIT0_ID; sub_id < CAPTURE_UNIT0_ID + N_CAPTURE_UNIT_ID;
	     sub_id++) {
		capture_unit_configure(ID,
				       sub_id,
				       &cfg->ctrl_unit_cfg[ID].buffer_mipi[sub_id - CAPTURE_UNIT0_ID]);
	}

	// Set acquisition units.
	for (sub_id = ACQUISITION_UNIT0_ID;
	     sub_id < ACQUISITION_UNIT0_ID + N_ACQUISITION_UNIT_ID; sub_id++) {
		acquisition_unit_configure(ID,
					   sub_id,
					   &cfg->ctrl_unit_cfg[sub_id - ACQUISITION_UNIT0_ID].buffer_acquire[sub_id -
						   ACQUISITION_UNIT0_ID]);
	}

	// No DMA configuration needed. Ctrl_unit will fully control it.

	// Set controller units.
	for (sub_id = CTRL_UNIT0_ID; sub_id < CTRL_UNIT0_ID + N_CTRL_UNIT_ID;
	     sub_id++) {
		ctrl_unit_configure(ID,
				    sub_id,
				    &cfg->ctrl_unit_cfg[sub_id - CTRL_UNIT0_ID]);
	}

	return;
}

static input_system_error_t configuration_to_registers(void)
{
	input_system_network_cfg_t input_system_network_cfg;
	int i;

	assert(config.source_type_flags & INPUT_SYSTEM_CFG_FLAG_SET);

	switch (config.source_type) {
	case INPUT_SYSTEM_SOURCE_SENSOR:

		// Determine stream multicasts setting based on the mode of csi_cfg_t.
		// AM: This should be moved towards earlier function call, e.g. in
		// the commit function.
		for (i = MIPI_PORT0_ID; i < N_MIPI_PORT_ID; i++) {
			if (config.csi_flags[i] & INPUT_SYSTEM_CFG_FLAG_SET) {
				switch (config.csi_value[i].buffering_mode) {
				case INPUT_SYSTEM_FIFO_CAPTURE:
					config.multicast[i] = INPUT_SYSTEM_CSI_BACKEND;
					break;

				case INPUT_SYSTEM_XMEM_CAPTURE:
				case INPUT_SYSTEM_SRAM_BUFFERING:
				case INPUT_SYSTEM_XMEM_BUFFERING:
					config.multicast[i] = INPUT_SYSTEM_INPUT_BUFFER;
					break;

				case INPUT_SYSTEM_FIFO_CAPTURE_WITH_COUNTING:
					config.multicast[i] = INPUT_SYSTEM_MULTICAST;
					break;

				case INPUT_SYSTEM_XMEM_ACQUIRE:
					config.multicast[i] = INPUT_SYSTEM_DISCARD_ALL;
					break;

				default:
					config.multicast[i] = INPUT_SYSTEM_DISCARD_ALL;
					return INPUT_SYSTEM_ERR_PARAMETER_NOT_SUPPORTED;
					//break;
				}
			} else {
				config.multicast[i] = INPUT_SYSTEM_DISCARD_ALL;
			}

			input_system_network_cfg.multicast_cfg[i] = config.multicast[i];

		} // for

		input_system_network_cfg.mux_cfg = config.multiplexer;

		input_system_network_cfg.ctrl_unit_cfg[CTRL_UNIT0_ID -
						       CTRL_UNIT0_ID].buffer_mipi[CAPTURE_UNIT0_ID] =
							       config.csi_buffer[MIPI_PORT0_ID];
		input_system_network_cfg.ctrl_unit_cfg[CTRL_UNIT0_ID -
						       CTRL_UNIT0_ID].buffer_mipi[CAPTURE_UNIT1_ID] =
							       config.csi_buffer[MIPI_PORT1_ID];
		input_system_network_cfg.ctrl_unit_cfg[CTRL_UNIT0_ID -
						       CTRL_UNIT0_ID].buffer_mipi[CAPTURE_UNIT2_ID] =
							       config.csi_buffer[MIPI_PORT2_ID];
		input_system_network_cfg.ctrl_unit_cfg[CTRL_UNIT0_ID -
						       CTRL_UNIT0_ID].buffer_acquire[ACQUISITION_UNIT0_ID -
							       ACQUISITION_UNIT0_ID] =
								       config.acquisition_buffer_unique;

		// First set input network around CSI receiver.
		input_system_network_configure(INPUT_SYSTEM0_ID, &input_system_network_cfg);

		// Set the CSI receiver.
		//...
		break;

	case INPUT_SYSTEM_SOURCE_TPG:

		break;

	case INPUT_SYSTEM_SOURCE_PRBS:

		break;

	case INPUT_SYSTEM_SOURCE_FIFO:
		break;

	default:
		return INPUT_SYSTEM_ERR_PARAMETER_NOT_SUPPORTED;
		break;

	} // end of switch (source_type)

	// Set input selector.
	input_selector_cfg_for_sensor(GP_DEVICE0_ID);

	// Set input switch.
	input_switch_cfg(GP_DEVICE0_ID, &config.input_switch_cfg);

	// Set input formatters.
	// AM: IF are set dynamically.
	return INPUT_SYSTEM_ERR_NO_ERROR;
}

// Function that applies the whole configuration.
input_system_error_t input_system_configuration_commit(void)
{
	// The last configuration step is to configure the input buffer.
	input_system_error_t error = input_buffer_configuration();

	if (error != INPUT_SYSTEM_ERR_NO_ERROR) {
		return error;
	}

	// Translate the whole configuration into registers.
	error = configuration_to_registers();
	if (error != INPUT_SYSTEM_ERR_NO_ERROR) {
		return error;
	}

	// Translate the whole configuration into ctrl commands etc.

	return INPUT_SYSTEM_ERR_NO_ERROR;
}

// FIFO

input_system_error_t	input_system_csi_fifo_channel_cfg(
    u32		ch_id,
    input_system_csi_port_t	port,
    backend_channel_cfg_t	backend_ch,
    target_cfg2400_t	target
)
{
	channel_cfg_t channel;

	channel.ch_id	= ch_id;
	channel.backend_ch	= backend_ch;
	channel.source_type = INPUT_SYSTEM_SOURCE_SENSOR;
	//channel.source
	channel.source_cfg.csi_cfg.csi_port			= port;
	channel.source_cfg.csi_cfg.buffering_mode	= INPUT_SYSTEM_FIFO_CAPTURE;
	channel.source_cfg.csi_cfg.csi_buffer			= IB_BUFFER_NULL;
	channel.source_cfg.csi_cfg.acquisition_buffer	= IB_BUFFER_NULL;
	channel.source_cfg.csi_cfg.nof_xmem_buffers	= 0;

	channel.target_cfg	= target;
	return input_system_configure_channel(channel);
}

input_system_error_t	input_system_csi_fifo_channel_with_counting_cfg(
    u32				ch_id,
    u32				nof_frames,
    input_system_csi_port_t			port,
    backend_channel_cfg_t			backend_ch,
    u32				csi_mem_reg_size,
    u32				csi_nof_mem_regs,
    target_cfg2400_t			target
)
{
	channel_cfg_t channel;

	channel.ch_id	= ch_id;
	channel.backend_ch	= backend_ch;
	channel.source_type		= INPUT_SYSTEM_SOURCE_SENSOR;
	//channel.source
	channel.source_cfg.csi_cfg.csi_port			= port;
	channel.source_cfg.csi_cfg.buffering_mode	=
	    INPUT_SYSTEM_FIFO_CAPTURE_WITH_COUNTING;
	channel.source_cfg.csi_cfg.csi_buffer.mem_reg_size		= csi_mem_reg_size;
	channel.source_cfg.csi_cfg.csi_buffer.nof_mem_regs		= csi_nof_mem_regs;
	channel.source_cfg.csi_cfg.csi_buffer.mem_reg_addr		= 0;
	channel.source_cfg.csi_cfg.acquisition_buffer			= IB_BUFFER_NULL;
	channel.source_cfg.csi_cfg.nof_xmem_buffers	= nof_frames;

	channel.target_cfg	= target;
	return input_system_configure_channel(channel);
}

// SRAM

input_system_error_t	input_system_csi_sram_channel_cfg(
    u32				ch_id,
    input_system_csi_port_t			port,
    backend_channel_cfg_t			backend_ch,
    u32				csi_mem_reg_size,
    u32				csi_nof_mem_regs,
    //	uint32_t				acq_mem_reg_size,
    //	uint32_t				acq_nof_mem_regs,
    target_cfg2400_t			target
)
{
	channel_cfg_t channel;

	channel.ch_id	= ch_id;
	channel.backend_ch	= backend_ch;
	channel.source_type		= INPUT_SYSTEM_SOURCE_SENSOR;
	//channel.source
	channel.source_cfg.csi_cfg.csi_port			= port;
	channel.source_cfg.csi_cfg.buffering_mode	= INPUT_SYSTEM_SRAM_BUFFERING;
	channel.source_cfg.csi_cfg.csi_buffer.mem_reg_size		= csi_mem_reg_size;
	channel.source_cfg.csi_cfg.csi_buffer.nof_mem_regs		= csi_nof_mem_regs;
	channel.source_cfg.csi_cfg.csi_buffer.mem_reg_addr		= 0;
	channel.source_cfg.csi_cfg.acquisition_buffer			= IB_BUFFER_NULL;
	channel.source_cfg.csi_cfg.nof_xmem_buffers	= 0;

	channel.target_cfg	= target;
	return input_system_configure_channel(channel);
}

//XMEM

// Collects all parameters and puts them in channel_cfg_t.
input_system_error_t	input_system_csi_xmem_channel_cfg(
    u32				ch_id,
    input_system_csi_port_t			port,
    backend_channel_cfg_t			backend_ch,
    u32				csi_mem_reg_size,
    u32				csi_nof_mem_regs,
    u32				acq_mem_reg_size,
    u32				acq_nof_mem_regs,
    target_cfg2400_t			target,
    uint32_t				nof_xmem_buffers
)
{
	channel_cfg_t channel;

	channel.ch_id	= ch_id;
	channel.backend_ch	= backend_ch;
	channel.source_type		= INPUT_SYSTEM_SOURCE_SENSOR;
	//channel.source
	channel.source_cfg.csi_cfg.csi_port			= port;
	channel.source_cfg.csi_cfg.buffering_mode	= INPUT_SYSTEM_XMEM_BUFFERING;
	channel.source_cfg.csi_cfg.csi_buffer.mem_reg_size		= csi_mem_reg_size;
	channel.source_cfg.csi_cfg.csi_buffer.nof_mem_regs		= csi_nof_mem_regs;
	channel.source_cfg.csi_cfg.csi_buffer.mem_reg_addr		= 0;
	channel.source_cfg.csi_cfg.acquisition_buffer.mem_reg_size	= acq_mem_reg_size;
	channel.source_cfg.csi_cfg.acquisition_buffer.nof_mem_regs	= acq_nof_mem_regs;
	channel.source_cfg.csi_cfg.acquisition_buffer.mem_reg_addr	= 0;
	channel.source_cfg.csi_cfg.nof_xmem_buffers	= nof_xmem_buffers;

	channel.target_cfg	= target;
	return input_system_configure_channel(channel);
}

input_system_error_t	input_system_csi_xmem_acquire_only_channel_cfg(
    u32				ch_id,
    u32				nof_frames,
    input_system_csi_port_t			port,
    backend_channel_cfg_t			backend_ch,
    u32				acq_mem_reg_size,
    u32				acq_nof_mem_regs,
    target_cfg2400_t			target)
{
	channel_cfg_t channel;

	channel.ch_id	= ch_id;
	channel.backend_ch	= backend_ch;
	channel.source_type		= INPUT_SYSTEM_SOURCE_SENSOR;
	//channel.source
	channel.source_cfg.csi_cfg.csi_port			= port;
	channel.source_cfg.csi_cfg.buffering_mode	= INPUT_SYSTEM_XMEM_ACQUIRE;
	channel.source_cfg.csi_cfg.csi_buffer		= IB_BUFFER_NULL;
	channel.source_cfg.csi_cfg.acquisition_buffer.mem_reg_size	= acq_mem_reg_size;
	channel.source_cfg.csi_cfg.acquisition_buffer.nof_mem_regs	= acq_nof_mem_regs;
	channel.source_cfg.csi_cfg.acquisition_buffer.mem_reg_addr	= 0;
	channel.source_cfg.csi_cfg.nof_xmem_buffers	= nof_frames;

	channel.target_cfg	= target;
	return input_system_configure_channel(channel);
}

input_system_error_t	input_system_csi_xmem_capture_only_channel_cfg(
    u32				ch_id,
    u32				nof_frames,
    input_system_csi_port_t			port,
    u32				csi_mem_reg_size,
    u32				csi_nof_mem_regs,
    u32				acq_mem_reg_size,
    u32				acq_nof_mem_regs,
    target_cfg2400_t			target)
{
	channel_cfg_t channel;

	channel.ch_id	= ch_id;
	//channel.backend_ch	= backend_ch;
	channel.source_type		= INPUT_SYSTEM_SOURCE_SENSOR;
	//channel.source
	channel.source_cfg.csi_cfg.csi_port			= port;
	//channel.source_cfg.csi_cfg.backend_ch		= backend_ch;
	channel.source_cfg.csi_cfg.buffering_mode	= INPUT_SYSTEM_XMEM_CAPTURE;
	channel.source_cfg.csi_cfg.csi_buffer.mem_reg_size		= csi_mem_reg_size;
	channel.source_cfg.csi_cfg.csi_buffer.nof_mem_regs		= csi_nof_mem_regs;
	channel.source_cfg.csi_cfg.csi_buffer.mem_reg_addr		= 0;
	channel.source_cfg.csi_cfg.acquisition_buffer.mem_reg_size	= acq_mem_reg_size;
	channel.source_cfg.csi_cfg.acquisition_buffer.nof_mem_regs	= acq_nof_mem_regs;
	channel.source_cfg.csi_cfg.acquisition_buffer.mem_reg_addr	= 0;
	channel.source_cfg.csi_cfg.nof_xmem_buffers	= nof_frames;

	channel.target_cfg	= target;
	return input_system_configure_channel(channel);
}

// Non - CSI

input_system_error_t	input_system_prbs_channel_cfg(
    u32		ch_id,
    u32		nof_frames,//not used yet
    u32		seed,
    u32		sync_gen_width,
    u32		sync_gen_height,
    u32		sync_gen_hblank_cycles,
    u32		sync_gen_vblank_cycles,
    target_cfg2400_t	target
)
{
	channel_cfg_t channel;

	(void)nof_frames;

	channel.ch_id	= ch_id;
	channel.source_type = INPUT_SYSTEM_SOURCE_PRBS;

	channel.source_cfg.prbs_cfg.seed = seed;
	channel.source_cfg.prbs_cfg.sync_gen_cfg.width		= sync_gen_width;
	channel.source_cfg.prbs_cfg.sync_gen_cfg.height		= sync_gen_height;
	channel.source_cfg.prbs_cfg.sync_gen_cfg.hblank_cycles	= sync_gen_hblank_cycles;
	channel.source_cfg.prbs_cfg.sync_gen_cfg.vblank_cycles	= sync_gen_vblank_cycles;

	channel.target_cfg	= target;

	return input_system_configure_channel(channel);
}

input_system_error_t	input_system_tpg_channel_cfg(
    u32		ch_id,
    u32		nof_frames,//not used yet
    u32		x_mask,
    u32		y_mask,
    u32		x_delta,
    u32		y_delta,
    u32		xy_mask,
    u32		sync_gen_width,
    u32		sync_gen_height,
    u32		sync_gen_hblank_cycles,
    u32		sync_gen_vblank_cycles,
    target_cfg2400_t	target
)
{
	channel_cfg_t channel;

	(void)nof_frames;

	channel.ch_id	= ch_id;
	channel.source_type		= INPUT_SYSTEM_SOURCE_TPG;

	channel.source_cfg.tpg_cfg.x_mask	= x_mask;
	channel.source_cfg.tpg_cfg.y_mask	= y_mask;
	channel.source_cfg.tpg_cfg.x_delta	= x_delta;
	channel.source_cfg.tpg_cfg.y_delta	= y_delta;
	channel.source_cfg.tpg_cfg.xy_mask	= xy_mask;
	channel.source_cfg.tpg_cfg.sync_gen_cfg.width		= sync_gen_width;
	channel.source_cfg.tpg_cfg.sync_gen_cfg.height		= sync_gen_height;
	channel.source_cfg.tpg_cfg.sync_gen_cfg.hblank_cycles	= sync_gen_hblank_cycles;
	channel.source_cfg.tpg_cfg.sync_gen_cfg.vblank_cycles	= sync_gen_vblank_cycles;

	channel.target_cfg	= target;
	return input_system_configure_channel(channel);
}

// MW: Don't use system specific names, (even in system specific files) "cfg2400" -> cfg
input_system_error_t	input_system_gpfifo_channel_cfg(
    u32		ch_id,
    u32		nof_frames, //not used yet

    target_cfg2400_t	target)
{
	channel_cfg_t channel;

	(void)nof_frames;

	channel.ch_id	= ch_id;
	channel.source_type	= INPUT_SYSTEM_SOURCE_FIFO;

	channel.target_cfg	= target;
	return input_system_configure_channel(channel);
}

///////////////////////////////////////////////////////////////////////////
//
// Private specialized functions for channel setting.
//
///////////////////////////////////////////////////////////////////////////

// Fills the parameters to config.csi_value[port]
static input_system_error_t input_system_configure_channel_sensor(
    const channel_cfg_t channel)
{
	const u32 port = channel.source_cfg.csi_cfg.csi_port;
	input_system_error_t status = INPUT_SYSTEM_ERR_NO_ERROR;

	input_system_multiplex_t mux;

	if (port >= N_INPUT_SYSTEM_PORTS)
		return INPUT_SYSTEM_ERR_GENERIC;

	//check if port > N_INPUT_SYSTEM_MULTIPLEX

	status = set_source_type(&config.source_type, channel.source_type,
				 &config.source_type_flags);
	if (status != INPUT_SYSTEM_ERR_NO_ERROR) return status;

	// Check for conflicts on source (implicitly on multicast, capture unit and input buffer).

	status = set_csi_cfg(&config.csi_value[port], &channel.source_cfg.csi_cfg,
			     &config.csi_flags[port]);
	if (status != INPUT_SYSTEM_ERR_NO_ERROR) return status;

	switch (channel.source_cfg.csi_cfg.buffering_mode) {
	case INPUT_SYSTEM_FIFO_CAPTURE:

		// Check for conflicts on mux.
		mux = INPUT_SYSTEM_MIPI_PORT0 + port;
		status = input_system_multiplexer_cfg(&config.multiplexer, mux,
						      &config.multiplexer_flags);
		if (status != INPUT_SYSTEM_ERR_NO_ERROR) return status;
		config.multicast[port] = INPUT_SYSTEM_CSI_BACKEND;

		// Shared resource, so it should be blocked.
		//config.mux_flags |= INPUT_SYSTEM_CFG_FLAG_BLOCKED;
		//config.csi_buffer_flags[port] |= INPUT_SYSTEM_CFG_FLAG_BLOCKED;
		//config.acquisition_buffer_unique_flags |= INPUT_SYSTEM_CFG_FLAG_BLOCKED;

		break;
	case INPUT_SYSTEM_SRAM_BUFFERING:

		// Check for conflicts on mux.
		mux = INPUT_SYSTEM_ACQUISITION_UNIT;
		status = input_system_multiplexer_cfg(&config.multiplexer, mux,
						      &config.multiplexer_flags);
		if (status != INPUT_SYSTEM_ERR_NO_ERROR) return status;
		config.multicast[port] = INPUT_SYSTEM_INPUT_BUFFER;

		// Shared resource, so it should be blocked.
		//config.mux_flags |= INPUT_SYSTEM_CFG_FLAG_BLOCKED;
		//config.csi_buffer_flags[port] |= INPUT_SYSTEM_CFG_FLAG_BLOCKED;
		//config.acquisition_buffer_unique_flags |= INPUT_SYSTEM_CFG_FLAG_BLOCKED;

		break;
	case INPUT_SYSTEM_XMEM_BUFFERING:

		// Check for conflicts on mux.
		mux = INPUT_SYSTEM_ACQUISITION_UNIT;
		status = input_system_multiplexer_cfg(&config.multiplexer, mux,
						      &config.multiplexer_flags);
		if (status != INPUT_SYSTEM_ERR_NO_ERROR) return status;
		config.multicast[port] = INPUT_SYSTEM_INPUT_BUFFER;

		// Shared resource, so it should be blocked.
		//config.mux_flags |= INPUT_SYSTEM_CFG_FLAG_BLOCKED;
		//config.csi_buffer_flags[port] |= INPUT_SYSTEM_CFG_FLAG_BLOCKED;
		//config.acquisition_buffer_unique_flags |= INPUT_SYSTEM_CFG_FLAG_BLOCKED;

		break;
	case INPUT_SYSTEM_FIFO_CAPTURE_WITH_COUNTING:
		return INPUT_SYSTEM_ERR_PARAMETER_NOT_SUPPORTED;
		break;
	case INPUT_SYSTEM_XMEM_CAPTURE:
		return INPUT_SYSTEM_ERR_PARAMETER_NOT_SUPPORTED;
		break;
	case INPUT_SYSTEM_XMEM_ACQUIRE:
		return INPUT_SYSTEM_ERR_PARAMETER_NOT_SUPPORTED;
		break;
	default:
		return INPUT_SYSTEM_ERR_PARAMETER_NOT_SUPPORTED;
		break;
	}
	return INPUT_SYSTEM_ERR_NO_ERROR;
}

// Test flags and set structure.
static input_system_error_t set_source_type(
    input_system_source_t *const lhs,
    const input_system_source_t			rhs,
    input_system_config_flags_t *const flags)
{
	// MW: Not enough asserts
	assert(lhs);
	assert(flags);

	if ((*flags) & INPUT_SYSTEM_CFG_FLAG_BLOCKED) {
		*flags |= INPUT_SYSTEM_CFG_FLAG_CONFLICT;
		return INPUT_SYSTEM_ERR_CONFLICT_ON_RESOURCE;
	}

	if ((*flags) & INPUT_SYSTEM_CFG_FLAG_SET) {
		// Check for consistency with already set value.
		if ((*lhs) == (rhs)) {
			return INPUT_SYSTEM_ERR_NO_ERROR;
		} else {
			*flags |= INPUT_SYSTEM_CFG_FLAG_CONFLICT;
			return INPUT_SYSTEM_ERR_CONFLICT_ON_RESOURCE;
		}
	}
	// Check the value (individually).
	if (rhs >= N_INPUT_SYSTEM_SOURCE) {
		*flags |= INPUT_SYSTEM_CFG_FLAG_CONFLICT;
		return INPUT_SYSTEM_ERR_CONFLICT_ON_RESOURCE;
	}
	// Set the value.
	*lhs = rhs;

	*flags |= INPUT_SYSTEM_CFG_FLAG_SET;
	return INPUT_SYSTEM_ERR_NO_ERROR;
}

// Test flags and set structure.
static input_system_error_t set_csi_cfg(
    csi_cfg_t *const lhs,
    const csi_cfg_t *const rhs,
    input_system_config_flags_t *const flags)
{
	u32 memory_required;
	u32 acq_memory_required;

	assert(lhs);
	assert(flags);

	if ((*flags) & INPUT_SYSTEM_CFG_FLAG_BLOCKED) {
		*flags |= INPUT_SYSTEM_CFG_FLAG_CONFLICT;
		return INPUT_SYSTEM_ERR_CONFLICT_ON_RESOURCE;
	}

	if (*flags & INPUT_SYSTEM_CFG_FLAG_SET) {
		// check for consistency with already set value.
		if (/*lhs->backend_ch == rhs.backend_ch
			&&*/ lhs->buffering_mode == rhs->buffering_mode
		    && lhs->csi_buffer.mem_reg_size == rhs->csi_buffer.mem_reg_size
		    && lhs->csi_buffer.nof_mem_regs  == rhs->csi_buffer.nof_mem_regs
		    && lhs->acquisition_buffer.mem_reg_size == rhs->acquisition_buffer.mem_reg_size
		    && lhs->acquisition_buffer.nof_mem_regs  == rhs->acquisition_buffer.nof_mem_regs
		    && lhs->nof_xmem_buffers  == rhs->nof_xmem_buffers
		) {
			return INPUT_SYSTEM_ERR_NO_ERROR;
		} else {
			*flags |= INPUT_SYSTEM_CFG_FLAG_CONFLICT;
			return INPUT_SYSTEM_ERR_CONFLICT_ON_RESOURCE;
		}
	}
	// Check the value (individually).
	// no check for backend_ch
	// no check for nof_xmem_buffers
	memory_required = rhs->csi_buffer.mem_reg_size * rhs->csi_buffer.nof_mem_regs;
	acq_memory_required = rhs->acquisition_buffer.mem_reg_size *
			      rhs->acquisition_buffer.nof_mem_regs;
	if (rhs->buffering_mode >= N_INPUT_SYSTEM_BUFFERING_MODE
	    ||
	    // Check if required memory is available in input buffer (SRAM).
	    (memory_required + acq_memory_required) > config.unallocated_ib_mem_words

	   ) {
		*flags |= INPUT_SYSTEM_CFG_FLAG_CONFLICT;
		return INPUT_SYSTEM_ERR_CONFLICT_ON_RESOURCE;
	}
	// Set the value.
	//lhs[port]->backend_ch		= rhs.backend_ch;
	lhs->buffering_mode	= rhs->buffering_mode;
	lhs->nof_xmem_buffers = rhs->nof_xmem_buffers;

	lhs->csi_buffer.mem_reg_size = rhs->csi_buffer.mem_reg_size;
	lhs->csi_buffer.nof_mem_regs  = rhs->csi_buffer.nof_mem_regs;
	lhs->acquisition_buffer.mem_reg_size = rhs->acquisition_buffer.mem_reg_size;
	lhs->acquisition_buffer.nof_mem_regs  = rhs->acquisition_buffer.nof_mem_regs;
	// ALX: NB: Here we just set buffer parameters, but still not allocate it
	// (no addresses determined). That will be done during commit.

	//  FIXIT:	acq_memory_required is not deducted, since it can be allocated multiple times.
	config.unallocated_ib_mem_words -= memory_required;
//assert(config.unallocated_ib_mem_words >=0);
	*flags |= INPUT_SYSTEM_CFG_FLAG_SET;
	return INPUT_SYSTEM_ERR_NO_ERROR;
}

// Test flags and set structure.
static input_system_error_t input_system_multiplexer_cfg(
    input_system_multiplex_t *const lhs,
    const input_system_multiplex_t		rhs,
    input_system_config_flags_t *const flags)
{
	assert(lhs);
	assert(flags);

	if ((*flags) & INPUT_SYSTEM_CFG_FLAG_BLOCKED) {
		*flags |= INPUT_SYSTEM_CFG_FLAG_CONFLICT;
		return INPUT_SYSTEM_ERR_CONFLICT_ON_RESOURCE;
	}

	if ((*flags) & INPUT_SYSTEM_CFG_FLAG_SET) {
		// Check for consistency with already set value.
		if ((*lhs) == (rhs)) {
			return INPUT_SYSTEM_ERR_NO_ERROR;
		} else {
			*flags |= INPUT_SYSTEM_CFG_FLAG_CONFLICT;
			return INPUT_SYSTEM_ERR_CONFLICT_ON_RESOURCE;
		}
	}
	// Check the value (individually).
	if (rhs >= N_INPUT_SYSTEM_MULTIPLEX) {
		*flags |= INPUT_SYSTEM_CFG_FLAG_CONFLICT;
		return INPUT_SYSTEM_ERR_PARAMETER_NOT_SUPPORTED;
	}
	// Set the value.
	*lhs = rhs;

	*flags |= INPUT_SYSTEM_CFG_FLAG_SET;
	return INPUT_SYSTEM_ERR_NO_ERROR;
}
#endif
