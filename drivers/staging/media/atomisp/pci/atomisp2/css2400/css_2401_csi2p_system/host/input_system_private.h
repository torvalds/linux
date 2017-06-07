/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
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

#ifndef __INPUT_SYSTEM_PRIVATE_H_INCLUDED__
#define __INPUT_SYSTEM_PRIVATE_H_INCLUDED__

#include "input_system_public.h"

STORAGE_CLASS_INPUT_SYSTEM_C input_system_err_t input_system_get_state(
	const input_system_ID_t	ID,
	input_system_state_t *state)
{
	uint32_t i;

	(void)(ID);

	/*  get the states of all CSI RX frontend devices */
	for (i = 0; i < N_CSI_RX_FRONTEND_ID; i++) {
		csi_rx_fe_ctrl_get_state(
				(csi_rx_frontend_ID_t)i,
				&(state->csi_rx_fe_ctrl_state[i]));
	}

	/*  get the states of all CIS RX backend devices */
	for (i = 0; i < N_CSI_RX_BACKEND_ID; i++) {
		csi_rx_be_ctrl_get_state(
				(csi_rx_backend_ID_t)i,
				&(state->csi_rx_be_ctrl_state[i]));
	}

	/* get the states of all pixelgen devices */
	for (i = 0; i < N_PIXELGEN_ID; i++) {
		pixelgen_ctrl_get_state(
				(pixelgen_ID_t)i,
				&(state->pixelgen_ctrl_state[i]));
	}

	/* get the states of all stream2mmio devices */
	for (i = 0; i < N_STREAM2MMIO_ID; i++) {
		stream2mmio_get_state(
				(stream2mmio_ID_t)i,
				&(state->stream2mmio_state[i]));
	}

	/* get the states of all ibuf-controller devices */
	for (i = 0; i < N_IBUF_CTRL_ID; i++) {
		ibuf_ctrl_get_state(
				(ibuf_ctrl_ID_t)i,
				&(state->ibuf_ctrl_state[i]));
	}

	/* get the states of all isys irq controllers */
	for (i = 0; i < N_ISYS_IRQ_ID; i++) {
		isys_irqc_state_get((isys_irq_ID_t)i, &(state->isys_irqc_state[i]));
	}

	/* TODO: get the states of all ISYS2401 DMA devices  */
	for (i = 0; i < N_ISYS2401_DMA_ID; i++) {
	}

	return INPUT_SYSTEM_ERR_NO_ERROR;
}
STORAGE_CLASS_INPUT_SYSTEM_C void input_system_dump_state(
	const input_system_ID_t	ID,
	input_system_state_t *state)
{
	uint32_t i;

	(void)(ID);

	/*  dump the states of all CSI RX frontend devices */
	for (i = 0; i < N_CSI_RX_FRONTEND_ID; i++) {
		csi_rx_fe_ctrl_dump_state(
				(csi_rx_frontend_ID_t)i,
				&(state->csi_rx_fe_ctrl_state[i]));
	}

	/*  dump the states of all CIS RX backend devices */
	for (i = 0; i < N_CSI_RX_BACKEND_ID; i++) {
		csi_rx_be_ctrl_dump_state(
				(csi_rx_backend_ID_t)i,
				&(state->csi_rx_be_ctrl_state[i]));
	}

	/* dump the states of all pixelgen devices */
	for (i = 0; i < N_PIXELGEN_ID; i++) {
		pixelgen_ctrl_dump_state(
				(pixelgen_ID_t)i,
				&(state->pixelgen_ctrl_state[i]));
	}

	/* dump the states of all st2mmio devices */
	for (i = 0; i < N_STREAM2MMIO_ID; i++) {
		stream2mmio_dump_state(
				(stream2mmio_ID_t)i,
				&(state->stream2mmio_state[i]));
	}

	/* dump the states of all ibuf-controller devices */
	for (i = 0; i < N_IBUF_CTRL_ID; i++) {
		ibuf_ctrl_dump_state(
				(ibuf_ctrl_ID_t)i,
				&(state->ibuf_ctrl_state[i]));
	}

	/* dump the states of all isys irq controllers */
	for (i = 0; i < N_ISYS_IRQ_ID; i++) {
		isys_irqc_state_dump((isys_irq_ID_t)i, &(state->isys_irqc_state[i]));
	}

	/* TODO: dump the states of all ISYS2401 DMA devices  */
	for (i = 0; i < N_ISYS2401_DMA_ID; i++) {
	}

	return;
}
#endif /* __INPUT_SYSTEM_PRIVATE_H_INCLUDED__ */
