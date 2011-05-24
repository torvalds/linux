/*
 * Broadcom SPI Low-Level Hardware Driver API
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: bcmspi.h,v 13.5.112.1 2010-11-15 18:13:09 Exp $
 */
#ifndef	_BCM_SPI_H
#define	_BCM_SPI_H

extern void spi_devintr_off(sdioh_info_t *sd);
extern void spi_devintr_on(sdioh_info_t *sd);
extern bool spi_start_clock(sdioh_info_t *sd, uint16 new_sd_divisor);
extern bool spi_controller_highspeed_mode(sdioh_info_t *sd, bool hsmode);
extern bool spi_check_client_intr(sdioh_info_t *sd, int *is_dev_intr);
extern bool spi_hw_attach(sdioh_info_t *sd);
extern bool spi_hw_detach(sdioh_info_t *sd);
extern void spi_sendrecv(sdioh_info_t *sd, uint8 *msg_out, uint8 *msg_in, int msglen);
extern void spi_spinbits(sdioh_info_t *sd);
extern void spi_waitbits(sdioh_info_t *sd, bool yield);

#endif /* _BCM_SPI_H */
