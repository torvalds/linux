/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __HAL_BTIF_H_
#define __HAL_BTIF_H_

#ifndef CONFIG_OF
#define MTK_BTIF_REG_BASE BTIF_BASE
#endif

#if defined(CONFIG_MTK_CLKMGR)
#if defined(CONFIG_ARCH_MT6580)
#define MTK_BTIF_CG_BIT MT_CG_BTIF_SW_CG
#elif defined(CONFIG_ARCH_MT6735) || defined(CONFIG_ARCH_MT6735M) || defined(CONFIG_ARCH_MT6753)
#define MTK_BTIF_CG_BIT MT_CG_PERI_BTIF
#endif
#else
struct clk *clk_btif_apdma; /*btif apdma clock*/
struct clk *clk_btif; /*btif  clock*/
#endif /* !defined(CONFIG_MTK_CLKMGR) */

#define BTIF_RBR(base)         (unsigned long)(base + 0x0)	/*RX Buffer Register: read only */
#define BTIF_THR(base)         (unsigned long)(base + 0x0) /*Rx Holding Register: write only */
#define BTIF_IER(base)         (unsigned long)(base + 0x4) /*Interrupt Enable Register: read/write */
#define BTIF_IIR(base)         (unsigned long)(base + 0x8)	/*Interrupt Identification Register: read only */
#define BTIF_FIFOCTRL(base)    (unsigned long)(base + 0x8)	/*FIFO Control Register: write only */
#define BTIF_FAKELCR(base)     (unsigned long)(base + 0xC)	/*FAKE LCR Register: read/write */
#define BTIF_LSR(base)         (unsigned long)(base + 0x14)	/*Line Status Register: read only */
#define BTIF_SLEEP_EN(base)    (unsigned long)(base + 0x48)	/*Sleep Enable Register: read/write */
#define BTIF_DMA_EN(base)      (unsigned long)(base + 0x4C)	/*DMA Enable Register: read/write */
#define BTIF_RTOCNT(base)      (unsigned long)(base + 0x54)	/*Rx Timeout Count Register: read/write */
#define BTIF_TRI_LVL(base)     (unsigned long)(base + 0x60)	/*Tx/Rx Trigger Level Control Register: read/write */
#define BTIF_WAK(base)         (unsigned long)(base + 0x64)	/*BTIF module wakeup Register: write only */
#define BTIF_WAT_TIME(base)    (unsigned long)(base + 0x68)	/*BTIF ASYNC Wait Time Control Register: read/write */
#define BTIF_HANDSHAKE(base)   (unsigned long)(base + 0x6C)	/*BTIF New Handshake Control Register: read/write */

/*BTIF_IER bits*/
#define BTIF_IER_TXEEN (0x1 << 1)	/*1: Tx holding register is empty */
#define BTIF_IER_RXFEN (0x1 << 0)	/*1: Rx buffer contains data */

/*BTIF_IIR bits*/
#define BTIF_IIR_NINT        (0x1 << 0)	/*No INT Pending */
#define BTIF_IIR_TX_EMPTY    (0x1 << 1)	/*Tx Holding Register empty */
#define BTIF_IIR_RX          (0x1 << 2)	/*Rx data received */
#define BTIF_IIR_RX_TIMEOUT  (0x11 << 2)	/*Rx data received */

/*BTIF_LSR bits*/
#define BTIF_LSR_DR_BIT (0x1 << 0)
#define BTIF_LSR_THRE_BIT (0x1 << 5)
#define BTIF_LSR_TEMT_BIT (0x1 << 6)

/*BTIF_FIFOCTRL bits*/
#define BTIF_FIFOCTRL_CLR_TX (0x1 << 2)	/*Clear Tx FIRO */
#define BTIF_FIFOCTRL_CLR_RX (0x1 << 1)	/*Clear Rx FIRO */

/*BTIF_FAKELCR bits*/
#define BTIF_FAKELCR_NORMAL_MODE 0x0

/*BTIF_SLEEP_EN bits*/
#define BTIF_SLEEP_EN_BIT (0x1 << 0)	/*enable Sleep mode */
#define BTIF_SLEEP_DIS_BIT (0x0)	/*disable sleep mode */

/*BTIF_DMA_EN bits*/
#define BTIF_DMA_EN_RX  (0x1 << 0)	/*Enable Rx DMA */
#define BTIF_DMA_EN_TX  (0x1 << 1)	/*Enable Tx DMA */
#define BTIF_DMA_EN_AUTORST_EN  (0x1 << 2)	/*1: timeout counter will be auto reset */
#define BTIF_DMA_EN_AUTORST_DIS  (0x0 << 2)	/*
						 * 0: after Rx timeout happens,
						 * SW shall reset the interrupt by reading BTIF 0x4C
						 */

/*BTIF_TRI_LVL bits*/
#define BTIF_TRI_LVL_TX_MASK ((0xf) << 0)
#define BTIF_TRI_LVL_RX_MASK ((0x7) << 4)

#define BTIF_TRI_LVL_TX(x) ((x & 0xf) << 0)
#define BTIF_TRI_LVL_RX(x) ((x & 0x7) << 4)

#define BTIF_TRI_LOOP_EN (0x1 << 7)
#define BTIF_TRI_LOOP_DIS (0x0 << 7)

/*BTIF_WAK bits*/
#define BTIF_WAK_BIT (0x1 << 0)

/*BTIF_HANDSHAKE bits*/
#define BTIF_HANDSHAKE_EN_HANDSHAKE 1
#define BTIF_HANDSHAKE_DIS_HANDSHAKE 0

#define BTIF_TX_FIFO_SIZE 16
#define BTIF_RX_FIFO_SIZE 8

#define BTIF_TX_FIFO_THRE (BTIF_TX_FIFO_SIZE / 2)
#define BTIF_RX_FIFO_THRE 0x1	/* 0x5 */

#endif /*__HAL_BTIF_H_*/
