/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __IA_CSS_IRQ_H
#define __IA_CSS_IRQ_H

/* @file
 * This file contains information for Interrupts/IRQs from CSS
 */

#include "ia_css_err.h"
#include "ia_css_pipe_public.h"
#include "ia_css_input_port.h"
#include <linux/bits.h>

/* Interrupt types, these enumerate all supported interrupt types.
 */
enum ia_css_irq_type {
	IA_CSS_IRQ_TYPE_EDGE,  /** Edge (level) sensitive interrupt */
	IA_CSS_IRQ_TYPE_PULSE  /** Pulse-shaped interrupt */
};

/* Interrupt request type.
 *  When the CSS hardware generates an interrupt, a function in this API
 *  needs to be called to retrieve information about the interrupt.
 *  This interrupt type is part of this information and indicates what
 *  type of information the interrupt signals.
 *
 *  Note that one interrupt can carry multiple interrupt types. For
 *  example: the online video ISP will generate only 2 interrupts, one to
 *  signal that the statistics (3a and DIS) are ready and one to signal
 *  that all output frames are done (output and viewfinder).
 *
 * DEPRECATED, this interface is not portable it should only define user
 * (SW) interrupts
 */
enum ia_css_irq_info {
	IA_CSS_IRQ_INFO_CSS_RECEIVER_ERROR            = BIT(0),
	/** the css receiver has encountered an error */
	IA_CSS_IRQ_INFO_CSS_RECEIVER_FIFO_OVERFLOW    = BIT(1),
	/** the FIFO in the csi receiver has overflown */
	IA_CSS_IRQ_INFO_CSS_RECEIVER_SOF              = BIT(2),
	/** the css receiver received the start of frame */
	IA_CSS_IRQ_INFO_CSS_RECEIVER_EOF              = BIT(3),
	/** the css receiver received the end of frame */
	IA_CSS_IRQ_INFO_CSS_RECEIVER_SOL              = BIT(4),
	/** the css receiver received the start of line */
	IA_CSS_IRQ_INFO_EVENTS_READY                  = BIT(5),
	/** One or more events are available in the PSYS event queue */
	IA_CSS_IRQ_INFO_CSS_RECEIVER_EOL              = BIT(6),
	/** the css receiver received the end of line */
	IA_CSS_IRQ_INFO_CSS_RECEIVER_SIDEBAND_CHANGED = BIT(7),
	/** the css receiver received a change in side band signals */
	IA_CSS_IRQ_INFO_CSS_RECEIVER_GEN_SHORT_0      = BIT(8),
	/** generic short packets (0) */
	IA_CSS_IRQ_INFO_CSS_RECEIVER_GEN_SHORT_1      = BIT(9),
	/** generic short packets (1) */
	IA_CSS_IRQ_INFO_IF_PRIM_ERROR                 = BIT(10),
	/** the primary input formatter (A) has encountered an error */
	IA_CSS_IRQ_INFO_IF_PRIM_B_ERROR               = BIT(11),
	/** the primary input formatter (B) has encountered an error */
	IA_CSS_IRQ_INFO_IF_SEC_ERROR                  = BIT(12),
	/** the secondary input formatter has encountered an error */
	IA_CSS_IRQ_INFO_STREAM_TO_MEM_ERROR           = BIT(13),
	/** the stream-to-memory device has encountered an error */
	IA_CSS_IRQ_INFO_SW_0                          = BIT(14),
	/** software interrupt 0 */
	IA_CSS_IRQ_INFO_SW_1                          = BIT(15),
	/** software interrupt 1 */
	IA_CSS_IRQ_INFO_SW_2                          = BIT(16),
	/** software interrupt 2 */
	IA_CSS_IRQ_INFO_ISP_BINARY_STATISTICS_READY   = BIT(17),
	/** ISP binary statistics are ready */
	IA_CSS_IRQ_INFO_INPUT_SYSTEM_ERROR            = BIT(18),
	/** the input system is in error */
	IA_CSS_IRQ_INFO_IF_ERROR                      = BIT(19),
	/** the input formatter is in error */
	IA_CSS_IRQ_INFO_DMA_ERROR                     = BIT(20),
	/** the dma is in error */
	IA_CSS_IRQ_INFO_ISYS_EVENTS_READY             = BIT(21),
	/** end-of-frame events are ready in the isys_event queue */
};

/* CSS receiver error types. Whenever the CSS receiver has encountered
 *  an error, this enumeration is used to indicate which errors have occurred.
 *
 *  Note that multiple error flags can be enabled at once and that this is in
 *  fact common (whenever an error occurs, it usually results in multiple
 *  errors).
 *
 * DEPRECATED: This interface is not portable, different systems have
 * different receiver types, or possibly none in case of tests systems.
 */
enum ia_css_rx_irq_info {
	IA_CSS_RX_IRQ_INFO_BUFFER_OVERRUN   = BIT(0),  /** buffer overrun */
	IA_CSS_RX_IRQ_INFO_ENTER_SLEEP_MODE = BIT(1),  /** entering sleep mode */
	IA_CSS_RX_IRQ_INFO_EXIT_SLEEP_MODE  = BIT(2),  /** exited sleep mode */
	IA_CSS_RX_IRQ_INFO_ECC_CORRECTED    = BIT(3),  /** ECC corrected */
	IA_CSS_RX_IRQ_INFO_ERR_SOT          = BIT(4),
	/** Start of transmission */
	IA_CSS_RX_IRQ_INFO_ERR_SOT_SYNC     = BIT(5),  /** SOT sync (??) */
	IA_CSS_RX_IRQ_INFO_ERR_CONTROL      = BIT(6),  /** Control (??) */
	IA_CSS_RX_IRQ_INFO_ERR_ECC_DOUBLE   = BIT(7),  /** Double ECC */
	IA_CSS_RX_IRQ_INFO_ERR_CRC          = BIT(8),  /** CRC error */
	IA_CSS_RX_IRQ_INFO_ERR_UNKNOWN_ID   = BIT(9),  /** Unknown ID */
	IA_CSS_RX_IRQ_INFO_ERR_FRAME_SYNC   = BIT(10), /** Frame sync error */
	IA_CSS_RX_IRQ_INFO_ERR_FRAME_DATA   = BIT(11), /** Frame data error */
	IA_CSS_RX_IRQ_INFO_ERR_DATA_TIMEOUT = BIT(12), /** Timeout occurred */
	IA_CSS_RX_IRQ_INFO_ERR_UNKNOWN_ESC  = BIT(13), /** Unknown escape seq. */
	IA_CSS_RX_IRQ_INFO_ERR_LINE_SYNC    = BIT(14), /** Line Sync error */
	IA_CSS_RX_IRQ_INFO_INIT_TIMEOUT     = BIT(15),
};

/* Interrupt info structure. This structure contains information about an
 *  interrupt. This needs to be used after an interrupt is received on the IA
 *  to perform the correct action.
 */
struct ia_css_irq {
	enum ia_css_irq_info type; /** Interrupt type. */
	unsigned int sw_irq_0_val; /** In case of SW interrupt 0, value. */
	unsigned int sw_irq_1_val; /** In case of SW interrupt 1, value. */
	unsigned int sw_irq_2_val; /** In case of SW interrupt 2, value. */
	struct ia_css_pipe *pipe;
	/** The image pipe that generated the interrupt. */
};

/* @brief Obtain interrupt information.
 *
 * @param[out] info	Pointer to the interrupt info. The interrupt
 *			information wil be written to this info.
 * @return		If an error is encountered during the interrupt info
 *			and no interrupt could be translated successfully, this
 *			will return IA_CSS_INTERNAL_ERROR. Otherwise
 *			0.
 *
 * This function is expected to be executed after an interrupt has been sent
 * to the IA from the CSS. This function returns information about the interrupt
 * which is needed by the IA code to properly handle the interrupt. This
 * information includes the image pipe, buffer type etc.
 */
int
ia_css_irq_translate(unsigned int *info);

/* @brief Get CSI receiver error info.
 *
 * @param[out] irq_bits	Pointer to the interrupt bits. The interrupt
 *			bits will be written this info.
 *			This will be the error bits that are enabled in the CSI
 *			receiver error register.
 * @return	None
 *
 * This function should be used whenever a CSI receiver error interrupt is
 * generated. It provides the detailed information (bits) on the exact error
 * that occurred.
 *
 *@deprecated {this function is DEPRECATED since it only works on CSI port 1.
 * Use the function below instead and specify the appropriate port.}
 */
void
ia_css_rx_get_irq_info(unsigned int *irq_bits);

/* @brief Get CSI receiver error info.
 *
 * @param[in]  port     Input port identifier.
 * @param[out] irq_bits	Pointer to the interrupt bits. The interrupt
 *			bits will be written this info.
 *			This will be the error bits that are enabled in the CSI
 *			receiver error register.
 * @return	None
 *
 * This function should be used whenever a CSI receiver error interrupt is
 * generated. It provides the detailed information (bits) on the exact error
 * that occurred.
 */
void
ia_css_rx_port_get_irq_info(enum mipi_port_id port, unsigned int *irq_bits);

/* @brief Clear CSI receiver error info.
 *
 * @param[in] irq_bits	The bits that should be cleared from the CSI receiver
 *			interrupt bits register.
 * @return	None
 *
 * This function should be called after ia_css_rx_get_irq_info has been called
 * and the error bits have been interpreted. It is advised to use the return
 * value of that function as the argument to this function to make sure no new
 * error bits get overwritten.
 *
 * @deprecated{this function is DEPRECATED since it only works on CSI port 1.
 * Use the function below instead and specify the appropriate port.}
 */
void
ia_css_rx_clear_irq_info(unsigned int irq_bits);

/* @brief Clear CSI receiver error info.
 *
 * @param[in] port      Input port identifier.
 * @param[in] irq_bits	The bits that should be cleared from the CSI receiver
 *			interrupt bits register.
 * @return	None
 *
 * This function should be called after ia_css_rx_get_irq_info has been called
 * and the error bits have been interpreted. It is advised to use the return
 * value of that function as the argument to this function to make sure no new
 * error bits get overwritten.
 */
void
ia_css_rx_port_clear_irq_info(enum mipi_port_id port, unsigned int irq_bits);

/* @brief Enable or disable specific interrupts.
 *
 * @param[in] type	The interrupt type that will be enabled/disabled.
 * @param[in] enable	enable or disable.
 * @return		Returns IA_CSS_INTERNAL_ERROR if this interrupt
 *			type cannot be enabled/disabled which is true for
 *			CSS internal interrupts. Otherwise returns
 *			0.
 */
int
ia_css_irq_enable(enum ia_css_irq_info type, bool enable);

#endif /* __IA_CSS_IRQ_H */
