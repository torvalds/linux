/*
 * Copyright (C) 2009  Cisco Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _ASM_MACH_POWERTV_INTERRUPTS_H_
#define _ASM_MACH_POWERTV_INTERRUPTS_H_

/*
 * Defines for all of the interrupt lines
 */

/* Definitions for backward compatibility */
#define kIrq_Uart1		irq_uart1

#define ibase 0

/*------------- Register: int_stat_3 */
/* 126 unused (bit 31) */
#define irq_asc2video		(ibase+126)	/* ASC 2 Video Interrupt */
#define irq_asc1video		(ibase+125)	/* ASC 1 Video Interrupt */
#define irq_comms_block_wd	(ibase+124)	/* ASC 1 Video Interrupt */
#define irq_fdma_mailbox	(ibase+123)	/* FDMA Mailbox Output */
#define irq_fdma_gp		(ibase+122)	/* FDMA GP Output */
#define irq_mips_pic		(ibase+121)	/* MIPS Performance Counter
						 * Interrupt */
#define irq_mips_timer		(ibase+120)	/* MIPS Timer Interrupt */
#define irq_memory_protect	(ibase+119)	/* Memory Protection Interrupt
						 * -- Ored by glue logic inside
						 *  SPARC ILC (see
						 *  INT_MEM_PROT_STAT, below,
						 *  for individual interrupts)
						 */
/* 118 unused (bit 22) */
#define irq_sbag		(ibase+117)	/* SBAG Interrupt -- Ored by
						 * glue logic inside SPARC ILC
						 * (see INT_SBAG_STAT, below,
						 * for individual interrupts) */
#define irq_qam_b_fec		(ibase+116)	/* QAM	B FEC Interrupt */
#define irq_qam_a_fec		(ibase+115)	/* QAM A FEC Interrupt */
/* 114 unused	(bit 18) */
#define irq_mailbox		(ibase+113)	/* Mailbox Debug Interrupt  --
						 * Ored by glue logic inside
						 * SPARC ILC (see
						 * INT_MAILBOX_STAT, below, for
						 * individual interrupts) */
#define irq_fuse_stat1		(ibase+112)	/* Fuse Status 1 */
#define irq_fuse_stat2		(ibase+111)	/* Fuse Status 2 */
#define irq_fuse_stat3		(ibase+110)	/* Blitter Interrupt / Fuse
						 * Status 3 */
#define irq_blitter		(ibase+110)	/* Blitter Interrupt / Fuse
						 * Status 3 */
#define irq_avc1_pp0		(ibase+109)	/* AVC Decoder #1 PP0
						 * Interrupt */
#define irq_avc1_pp1		(ibase+108)	/* AVC Decoder #1 PP1
						 * Interrupt */
#define irq_avc1_mbe		(ibase+107)	/* AVC Decoder #1 MBE
						 * Interrupt */
#define irq_avc2_pp0		(ibase+106)	/* AVC Decoder #2 PP0
						 * Interrupt */
#define irq_avc2_pp1		(ibase+105)	/* AVC Decoder #2 PP1
						 * Interrupt */
#define irq_avc2_mbe		(ibase+104)	/* AVC Decoder #2 MBE
						 * Interrupt */
#define irq_zbug_spi		(ibase+103)	/* Zbug SPI Slave Interrupt */
#define irq_qam_mod2		(ibase+102)	/* QAM Modulator 2 DMA
						 * Interrupt */
#define irq_ir_rx		(ibase+101)	/* IR RX 2 Interrupt */
#define irq_aud_dsp2		(ibase+100)	/* Audio DSP #2 Interrupt */
#define irq_aud_dsp1		(ibase+99)	/* Audio DSP #1 Interrupt */
#define irq_docsis		(ibase+98)	/* DOCSIS Debug Interrupt */
#define irq_sd_dvp1		(ibase+97)	/* SD DVP #1 Interrupt */
#define irq_sd_dvp2		(ibase+96)	/* SD DVP #2 Interrupt */
/*------------- Register: int_stat_2 */
#define irq_hd_dvp		(ibase+95)	/* HD DVP Interrupt */
#define kIrq_Prewatchdog	(ibase+94)	/* watchdog Pre-Interrupt */
#define irq_timer2		(ibase+93)	/* Programmable Timer
						 * Interrupt 2 */
#define irq_1394		(ibase+92)	/* 1394 Firewire Interrupt */
#define irq_usbohci		(ibase+91)	/* USB 2.0 OHCI Interrupt */
#define irq_usbehci		(ibase+90)	/* USB 2.0 EHCI Interrupt */
#define irq_pciexp		(ibase+89)	/* PCI Express 0 Interrupt */
#define irq_pciexp0		(ibase+89)	/* PCI Express 0 Interrupt */
#define irq_afe1		(ibase+88)	/* AFE 1 Interrupt */
#define irq_sata		(ibase+87)	/* SATA 1 Interrupt */
#define irq_sata1		(ibase+87)	/* SATA 1 Interrupt */
#define irq_dtcp		(ibase+86)	/* DTCP Interrupt */
#define irq_pciexp1		(ibase+85)	/* PCI Express 1 Interrupt */
/* 84 unused	(bit 20) */
/* 83 unused	(bit 19) */
/* 82 unused	(bit 18) */
#define irq_sata2		(ibase+81)	/* SATA2 Interrupt */
#define irq_uart2		(ibase+80)	/* UART2 Interrupt */
#define irq_legacy_usb		(ibase+79)	/* Legacy USB Host ISR (1.1
						 * Host module) */
#define irq_pod			(ibase+78)	/* POD Interrupt */
#define irq_slave_usb		(ibase+77)	/* Slave USB */
#define irq_denc1		(ibase+76)	/* DENC #1 VTG Interrupt */
#define irq_vbi_vtg		(ibase+75)	/* VBI VTG Interrupt */
#define irq_afe2		(ibase+74)	/* AFE 2 Interrupt */
#define irq_denc2		(ibase+73)	/* DENC #2 VTG Interrupt */
#define irq_asc2		(ibase+72)	/* ASC #2 Interrupt */
#define irq_asc1		(ibase+71)	/* ASC #1 Interrupt */
#define irq_mod_dma		(ibase+70)	/* Modulator DMA Interrupt */
#define irq_byte_eng1		(ibase+69)	/* Byte Engine Interrupt [1] */
#define irq_byte_eng0		(ibase+68)	/* Byte Engine Interrupt [0] */
/* 67 unused	(bit 03) */
/* 66 unused	(bit 02) */
/* 65 unused	(bit 01) */
/* 64 unused	(bit 00) */
/*------------- Register: int_stat_1 */
/* 63 unused	(bit 31) */
/* 62 unused	(bit 30) */
/* 61 unused	(bit 29) */
/* 60 unused	(bit 28) */
/* 59 unused	(bit 27) */
/* 58 unused	(bit 26) */
/* 57 unused	(bit 25) */
/* 56 unused	(bit 24) */
#define irq_buf_dma_mem2mem	(ibase+55)	/* BufDMA Memory to Memory
						 * Interrupt */
#define irq_buf_dma_usbtransmit (ibase+54)	/* BufDMA USB Transmit
						 * Interrupt */
#define irq_buf_dma_qpskpodtransmit (ibase+53)	/* BufDMA QPSK/POD Tramsit
						 * Interrupt */
#define irq_buf_dma_transmit_error (ibase+52)	/* BufDMA Transmit Error
						 * Interrupt */
#define irq_buf_dma_usbrecv	(ibase+51)	/* BufDMA USB Receive
						 * Interrupt */
#define irq_buf_dma_qpskpodrecv (ibase+50)	/* BufDMA QPSK/POD Receive
						 * Interrupt */
#define irq_buf_dma_recv_error	(ibase+49)	/* BufDMA Receive Error
						 * Interrupt */
#define irq_qamdma_transmit_play (ibase+48)	/* QAMDMA Transmit/Play
						 * Interrupt */
#define irq_qamdma_transmit_error (ibase+47)	/* QAMDMA Transmit Error
						 * Interrupt */
#define irq_qamdma_recv2high	(ibase+46)	/* QAMDMA Receive 2 High
						 * (Chans 63-32) */
#define irq_qamdma_recv2low	(ibase+45)	/* QAMDMA Receive 2 Low
						 * (Chans 31-0) */
#define irq_qamdma_recv1high	(ibase+44)	/* QAMDMA Receive 1 High
						 * (Chans 63-32) */
#define irq_qamdma_recv1low	(ibase+43)	/* QAMDMA Receive 1 Low
						 * (Chans 31-0) */
#define irq_qamdma_recv_error	(ibase+42)	/* QAMDMA Receive Error
						 * Interrupt */
#define irq_mpegsplice		(ibase+41)	/* MPEG Splice Interrupt */
#define irq_deinterlace_rdy	(ibase+40)	/* Deinterlacer Frame Ready
						 * Interrupt */
#define irq_ext_in0		(ibase+39)	/* External Interrupt irq_in0 */
#define irq_gpio3		(ibase+38)	/* GP I/O IRQ 3 - From GP I/O
						 * Module */
#define irq_gpio2		(ibase+37)	/* GP I/O IRQ 2 - From GP I/O
						 * Module (ABE_intN) */
#define irq_pcrcmplt1		(ibase+36)	/* PCR Capture Complete	 or
						 * Discontinuity 1 */
#define irq_pcrcmplt2		(ibase+35)	/* PCR Capture Complete or
						 * Discontinuity 2 */
#define irq_parse_peierr	(ibase+34)	/* PID Parser Error Detect
						 * (PEI) */
#define irq_parse_cont_err	(ibase+33)	/* PID Parser continuity error
						 * detect */
#define irq_ds1framer		(ibase+32)	/* DS1 Framer Interrupt */
/*------------- Register: int_stat_0 */
#define irq_gpio1		(ibase+31)	/* GP I/O IRQ 1 - From GP I/O
						 * Module */
#define irq_gpio0		(ibase+30)	/* GP I/O IRQ 0 - From GP I/O
						 * Module */
#define irq_qpsk_out_aloha	(ibase+29)	/* QPSK Output Slotted Aloha
						 * (chan 3) Transmission
						 * Completed OK */
#define irq_qpsk_out_tdma	(ibase+28)	/* QPSK Output TDMA (chan 2)
						 * Transmission Completed OK */
#define irq_qpsk_out_reserve	(ibase+27)	/* QPSK Output Reservation
						 * (chan 1) Transmission
						 * Completed OK */
#define irq_qpsk_out_aloha_err	(ibase+26)	/* QPSK Output Slotted Aloha
						 * (chan 3)Transmission
						 * completed with Errors. */
#define irq_qpsk_out_tdma_err	(ibase+25)	/* QPSK Output TDMA (chan 2)
						 * Transmission completed with
						 * Errors. */
#define irq_qpsk_out_rsrv_err	(ibase+24)	/* QPSK Output Reservation
						 * (chan 1) Transmission
						 * completed with Errors */
#define irq_aloha_fail		(ibase+23)	/* Unsuccessful Resend of Aloha
						 * for N times. Aloha retry
						 * timeout for channel 3. */
#define irq_timer1		(ibase+22)	/* Programmable Timer
						 * Interrupt */
#define irq_keyboard		(ibase+21)	/* Keyboard Module Interrupt */
#define irq_i2c			(ibase+20)	/* I2C Module Interrupt */
#define irq_spi			(ibase+19)	/* SPI Module Interrupt */
#define irq_irblaster		(ibase+18)	/* IR Blaster Interrupt */
#define irq_splice_detect	(ibase+17)	/* PID Key Change Interrupt or
						 * Splice Detect Interrupt */
#define irq_se_micro		(ibase+16)	/* Secure Micro I/F Module
						 * Interrupt */
#define irq_uart1		(ibase+15)	/* UART Interrupt */
#define irq_irrecv		(ibase+14)	/* IR Receiver Interrupt */
#define irq_host_int1		(ibase+13)	/* Host-to-Host Interrupt 1 */
#define irq_host_int0		(ibase+12)	/* Host-to-Host Interrupt 0 */
#define irq_qpsk_hecerr		(ibase+11)	/* QPSK HEC Error Interrupt */
#define irq_qpsk_crcerr		(ibase+10)	/* QPSK AAL-5 CRC Error
						 * Interrupt */
/* 9 unused	(bit 09) */
/* 8 unused	(bit 08) */
#define irq_psicrcerr		(ibase+7)	/* QAM PSI CRC Error
						 * Interrupt */
#define irq_psilength_err	(ibase+6)	/* QAM PSI Length Error
						 * Interrupt */
#define irq_esfforward		(ibase+5)	/* ESF Interrupt Mark From
						 * Forward Path Reference -
						 * every 3ms when forward Mbits
						 * and forward slot control
						 * bytes are updated. */
#define irq_esfreverse		(ibase+4)	/* ESF Interrupt Mark from
						 * Reverse Path Reference -
						 * delayed from forward mark by
						 * the ranging delay plus a
						 * fixed amount. When reverse
						 * Mbits and reverse slot
						 * control bytes are updated.
						 * Occurs every 3ms for 3.0M and
						 * 1.554 M upstream rates and
						 * every 6 ms for 256K upstream
						 * rate. */
#define irq_aloha_timeout	(ibase+3)	/* Slotted-Aloha timeout on
						 * Channel 1. */
#define irq_reservation		(ibase+2)	/* Partial (or Incremental)
						 * Reservation Message Completed
						 * or Slotted aloha verify for
						 * channel 1. */
#define irq_aloha3		(ibase+1)	/* Slotted-Aloha Message Verify
						 * Interrupt or Reservation
						 * increment completed for
						 * channel 3. */
#define irq_mpeg_d		(ibase+0)	/* MPEG Decoder Interrupt */
#endif	/* _ASM_MACH_POWERTV_INTERRUPTS_H_ */
