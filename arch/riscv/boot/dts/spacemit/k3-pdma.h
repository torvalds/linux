/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * DMA request number (DRQ) definitions for non-secure peripherals of
 * the SpacemiT K3 PDMA.
 *
 * Copyright (c) 2025 SpacemiT
 * Copyright (c) 2026 Guodong Xu <docular.xu@gmail.com>
 */

#ifndef _DTS_SPACEMIT_K3_PDMA_H
#define _DTS_SPACEMIT_K3_PDMA_H

/* UART DMA request numbers */
#define K3_PDMA_UART0_TX	3
#define K3_PDMA_UART0_RX	4
#define K3_PDMA_UART2_TX	5
#define K3_PDMA_UART2_RX	6
#define K3_PDMA_UART3_TX	7
#define K3_PDMA_UART3_RX	8
#define K3_PDMA_UART4_TX	9
#define K3_PDMA_UART4_RX	10
#define K3_PDMA_UART5_TX	25
#define K3_PDMA_UART5_RX	26
#define K3_PDMA_UART6_TX	27
#define K3_PDMA_UART6_RX	28
#define K3_PDMA_UART7_TX	29
#define K3_PDMA_UART7_RX	30
#define K3_PDMA_UART8_TX	31
#define K3_PDMA_UART8_RX	32
#define K3_PDMA_UART9_TX	33
#define K3_PDMA_UART9_RX	34
#define K3_PDMA_UART10_TX	53
#define K3_PDMA_UART10_RX	54

/* I2C DMA request numbers */
#define K3_PDMA_I2C0_TX	11
#define K3_PDMA_I2C0_RX	12
#define K3_PDMA_I2C1_TX	13
#define K3_PDMA_I2C1_RX	14
#define K3_PDMA_I2C2_TX	15
#define K3_PDMA_I2C2_RX	16
#define K3_PDMA_I2C4_TX	17
#define K3_PDMA_I2C4_RX	18
#define K3_PDMA_I2C5_TX	35
#define K3_PDMA_I2C5_RX	36
#define K3_PDMA_I2C6_TX	37
#define K3_PDMA_I2C6_RX	38
#define K3_PDMA_I2C8_TX	41
#define K3_PDMA_I2C8_RX	42

/* SSP/SPI DMA request numbers */
#define K3_PDMA_SSP3_TX	19
#define K3_PDMA_SSP3_RX	20
#define K3_PDMA_SSPA0_TX	21
#define K3_PDMA_SSPA0_RX	22
#define K3_PDMA_SSPA1_TX	23
#define K3_PDMA_SSPA1_RX	24
#define K3_PDMA_SSPA2_TX	56
#define K3_PDMA_SSPA2_RX	57
#define K3_PDMA_SSPA3_TX	58
#define K3_PDMA_SSPA3_RX	59
#define K3_PDMA_SSPA4_TX	60
#define K3_PDMA_SSPA4_RX	61
#define K3_PDMA_SSPA5_TX	62
#define K3_PDMA_SSPA5_RX	63

/* CAN DMA request numbers */
#define K3_PDMA_CAN0_RX	43
#define K3_PDMA_CAN1_RX	44
#define K3_PDMA_CAN2_RX	51
#define K3_PDMA_CAN3_RX	52

/* SSP0/1 DMA request numbers */
#define K3_PDMA_SSP0_TX	64
#define K3_PDMA_SSP0_RX	65
#define K3_PDMA_SSP1_TX	66
#define K3_PDMA_SSP1_RX	67

/* QSPI DMA request numbers */
#define K3_PDMA_QSPI_RX	84
#define K3_PDMA_QSPI_TX	85

#endif /* _DTS_SPACEMIT_K3_PDMA_H */
