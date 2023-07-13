/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
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

#ifndef __IA_CSS_ISYS_H__
#define __IA_CSS_ISYS_H__

#include <type_support.h>
#include <input_system.h>
#include <ia_css_input_port.h>
#include <ia_css_stream_format.h>
#include <ia_css_stream_public.h>
#include <system_global.h>
#include "ia_css_isys_comm.h"

/**
 * Virtual Input System. (Input System 2401)
 */
typedef isp2401_input_system_cfg_t	ia_css_isys_descr_t;
/* end of Virtual Input System */


input_system_err_t ia_css_isys_init(void);
void ia_css_isys_uninit(void);
enum mipi_port_id ia_css_isys_port_to_mipi_port(
    enum mipi_port_id api_port);


/**
 * @brief Register one (virtual) stream. This is used to track when all
 * virtual streams are configured inside the input system. The CSI RX is
 * only started when all registered streams are configured.
 *
 * @param[in]	port		CSI port
 * @param[in]	isys_stream_id	Stream handle generated with ia_css_isys_generate_stream_id()
 *				Must be lower than SH_CSS_MAX_ISYS_CHANNEL_NODES
 * @return			0 if successful, -EINVAL if
 *				there is already a stream registered with the same handle
 */
int ia_css_isys_csi_rx_register_stream(
    enum mipi_port_id port,
    uint32_t isys_stream_id);

/**
 * @brief Unregister one (virtual) stream. This is used to track when all
 * virtual streams are configured inside the input system. The CSI RX is
 * only started when all registered streams are configured.
 *
 * @param[in]	port		CSI port
 * @param[in]	isys_stream_id	Stream handle generated with ia_css_isys_generate_stream_id()
 *				Must be lower than SH_CSS_MAX_ISYS_CHANNEL_NODES
 * @return			0 if successful, -EINVAL if
 *				there is no stream registered with that handle
 */
int ia_css_isys_csi_rx_unregister_stream(
    enum mipi_port_id port,
    uint32_t isys_stream_id);

int ia_css_isys_convert_compressed_format(
    struct ia_css_csi2_compression *comp,
    struct isp2401_input_system_cfg_s *cfg);
unsigned int ia_css_csi2_calculate_input_system_alignment(
    enum atomisp_input_format fmt_type);

/* CSS Receiver */
void ia_css_isys_rx_configure(
    const rx_cfg_t *config,
    const enum ia_css_input_mode input_mode);

void ia_css_isys_rx_disable(void);

void ia_css_isys_rx_enable_all_interrupts(enum mipi_port_id port);

unsigned int ia_css_isys_rx_get_interrupt_reg(enum mipi_port_id port);
void ia_css_isys_rx_get_irq_info(enum mipi_port_id port,
				 unsigned int *irq_infos);
void ia_css_isys_rx_clear_irq_info(enum mipi_port_id port,
				   unsigned int irq_infos);
unsigned int ia_css_isys_rx_translate_irq_infos(unsigned int bits);


/* @brief Translate format and compression to format type.
 *
 * @param[in]	input_format	The input format.
 * @param[in]	compression	The compression scheme.
 * @param[out]	fmt_type	Pointer to the resulting format type.
 * @return			Error code.
 *
 * Translate an input format and mipi compression pair to the fmt_type.
 * This is normally done by the sensor, but when using the input fifo, this
 * format type must be sumitted correctly by the application.
 */
int ia_css_isys_convert_stream_format_to_mipi_format(
    enum atomisp_input_format input_format,
    mipi_predictor_t compression,
    unsigned int *fmt_type);

/**
 * Virtual Input System. (Input System 2401)
 */
ia_css_isys_error_t ia_css_isys_stream_create(
    ia_css_isys_descr_t	*isys_stream_descr,
    ia_css_isys_stream_h	isys_stream,
    uint32_t isys_stream_id);

void ia_css_isys_stream_destroy(
    ia_css_isys_stream_h	isys_stream);

ia_css_isys_error_t ia_css_isys_stream_calculate_cfg(
    ia_css_isys_stream_h		isys_stream,
    ia_css_isys_descr_t		*isys_stream_descr,
    ia_css_isys_stream_cfg_t	*isys_stream_cfg);

void ia_css_isys_csi_rx_lut_rmgr_init(void);

void ia_css_isys_csi_rx_lut_rmgr_uninit(void);

bool ia_css_isys_csi_rx_lut_rmgr_acquire(
    csi_rx_backend_ID_t		backend,
    csi_mipi_packet_type_t		packet_type,
    csi_rx_backend_lut_entry_t	*entry);

void ia_css_isys_csi_rx_lut_rmgr_release(
    csi_rx_backend_ID_t		backend,
    csi_mipi_packet_type_t		packet_type,
    csi_rx_backend_lut_entry_t	*entry);

void ia_css_isys_ibuf_rmgr_init(void);

void ia_css_isys_ibuf_rmgr_uninit(void);

bool ia_css_isys_ibuf_rmgr_acquire(
    u32	size,
    uint32_t	*start_addr);

void ia_css_isys_ibuf_rmgr_release(
    uint32_t	*start_addr);

void ia_css_isys_dma_channel_rmgr_init(void);

void ia_css_isys_dma_channel_rmgr_uninit(void);

bool ia_css_isys_dma_channel_rmgr_acquire(
    isys2401_dma_ID_t	dma_id,
    isys2401_dma_channel	*channel);

void ia_css_isys_dma_channel_rmgr_release(
    isys2401_dma_ID_t	dma_id,
    isys2401_dma_channel	*channel);

void ia_css_isys_stream2mmio_sid_rmgr_init(void);

void ia_css_isys_stream2mmio_sid_rmgr_uninit(void);

bool ia_css_isys_stream2mmio_sid_rmgr_acquire(
    stream2mmio_ID_t	stream2mmio,
    stream2mmio_sid_ID_t	*sid);

void ia_css_isys_stream2mmio_sid_rmgr_release(
    stream2mmio_ID_t	stream2mmio,
    stream2mmio_sid_ID_t	*sid);

/* end of Virtual Input System */

#endif				/* __IA_CSS_ISYS_H__ */
