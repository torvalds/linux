/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  OSLEC - A line echo canceller.  This code is being developed
 *          against and partially complies with G168. Using code from SpanDSP
 *
 * Written by Steve Underwood <steveu@coppice.org>
 *         and David Rowe <david_at_rowetel_dot_com>
 *
 * Copyright (C) 2001 Steve Underwood and 2007-2008 David Rowe
 *
 * All rights reserved.
 */

#ifndef __OSLEC_H
#define __OSLEC_H

/* Mask bits for the adaption mode */
#define ECHO_CAN_USE_ADAPTION	0x01
#define ECHO_CAN_USE_NLP	0x02
#define ECHO_CAN_USE_CNG	0x04
#define ECHO_CAN_USE_CLIP	0x08
#define ECHO_CAN_USE_TX_HPF	0x10
#define ECHO_CAN_USE_RX_HPF	0x20
#define ECHO_CAN_DISABLE	0x40

/**
 * oslec_state: G.168 echo canceller descriptor.
 *
 * This defines the working state for a line echo canceller.
 */
struct oslec_state;

/**
 * oslec_create - Create a voice echo canceller context.
 * @len: The length of the canceller, in samples.
 * @return: The new canceller context, or NULL if the canceller could not be
 * created.
 */
struct oslec_state *oslec_create(int len, int adaption_mode);

/**
 * oslec_free - Free a voice echo canceller context.
 * @ec: The echo canceller context.
 */
void oslec_free(struct oslec_state *ec);

/**
 * oslec_flush - Flush (reinitialise) a voice echo canceller context.
 * @ec: The echo canceller context.
 */
void oslec_flush(struct oslec_state *ec);

/**
 * oslec_adaption_mode - set the adaption mode of a voice echo canceller context.
 * @ec The echo canceller context.
 * @adaption_mode: The mode.
 */
void oslec_adaption_mode(struct oslec_state *ec, int adaption_mode);

void oslec_snapshot(struct oslec_state *ec);

/**
 * oslec_update: Process a sample through a voice echo canceller.
 * @ec: The echo canceller context.
 * @tx: The transmitted audio sample.
 * @rx: The received audio sample.
 *
 * The return value is the clean (echo cancelled) received sample.
 */
int16_t oslec_update(struct oslec_state *ec, int16_t tx, int16_t rx);

/**
 * oslec_hpf_tx: Process to high pass filter the tx signal.
 * @ec: The echo canceller context.
 * @tx: The transmitted auio sample.
 *
 * The return value is the HP filtered transmit sample, send this to your D/A.
 */
int16_t oslec_hpf_tx(struct oslec_state *ec, int16_t tx);

#endif /* __OSLEC_H */
