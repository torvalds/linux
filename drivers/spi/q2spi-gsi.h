/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __SPI_Q2SPI_GPI_H_
#define __SPI_Q2SPI_GPI_H_

/* Q2SPI Config0 TRE */
#define MSM_GPI_Q2SPI_CONFIG0_TRE_DWORD0(tsn, pack, tdn, cs_mode, intr_pol, word_size) \
	(((tsn) << 27) | ((pack) << 24) | \
	((tdn) << 14) | ((cs_mode) << 6) | ((intr_pol) << 5) | (word_size))
#define MSM_GPI_Q2SPI_CONFIG0_TRE_DWORD1(tan, cs_clk_del, ssn) \
	((tan) | ((cs_clk_del) << 8) | ((ssn) << 16))
#define MSM_GPI_Q2SPI_CONFIG0_TRE_DWORD2(cn_delay, clk_src, clk_div) (((cn_delay) << 20) | \
	((clk_src) << 16) | (clk_div))
#define MSM_GPI_Q2SPI_CONFIG0_TRE_DWORD3(link_rx, bei, ieot, ieob, ch) \
	((0x2 << 20) | (0x2 << 16) | ((link_rx) << 11) | ((bei) << 10) | \
	((ieot) << 9) | ((ieob) << 8) | (ch))

/* Q2SPI Go TRE */
#define MSM_GPI_Q2SPI_GO_TRE_DWORD0(flags, cs, cmd) (((flags) << 17) | \
	((cs) << 8) | (cmd))
#define MSM_GPI_Q2SPI_GO_TRE_DWORD1 (0)
#define MSM_GPI_Q2SPI_GO_TRE_DWORD2(rx_len) (rx_len)
#define MSM_GPI_Q2SPI_GO_TRE_DWORD3(link_rx, bei, ieot, ieob, ch) ((0x2 << 20) | \
	(0x0 << 16) | ((link_rx) << 11) | ((bei) << 10) | ((ieot) << 9) | \
	((ieob) << 8) | (ch))

/**
 * struct q2spi_gsi - structure to store gsi information for q2spi driver
 *
 * @tx_c: TX DMA channel
 * @rx_c: RX DMA channel
 * @config0_tre: stores config0 tre info
 * @go_tre: stores go tre info
 * @tx_dma_tre: stores DMA TX tre info
 * @rx_dma_tre: stores DMA RX tre info
 * @tx_ev: control structure to config gpi dma engine via dmaengine_slave_config() for tx.
 * @rx_ev: control structure to config gpi dma engine via dmaengine_slave_config() for rx.
 * @tx_desc: async transaction descriptor for tx
 * @rx_desc: async transaction descriptor for rx
 * @tx_cb_param: gpi specific callback parameters to pass between gpi client and gpi engine for TX.
 * @rx_cb_param: gpi specific callback parameters to pass between gpi client and gpi engine for RX.
 * @chan_setup: flag to mark channel setup completion.
 * @tx_sg: sg table for TX transfers
 * @rx_sg: sg table for RX transfers
 * tx_cookie: Represents dma tx cookie
 * rx_cookie: Represents dma rx cookie
 * num_tx_eot: Represents number of TX End of Transfers
 * num_rx_eot: Represents number of RX End of Transfers
 * qup_gsi_err: flag to represent gsi error if any
 * qup_gsi_global_err: flag to represent gsi global error
 */
struct q2spi_gsi {
	struct dma_chan *tx_c;
	struct dma_chan *rx_c;
	struct msm_gpi_tre config0_tre;
	struct msm_gpi_tre go_tre;
	struct msm_gpi_tre tx_dma_tre;
	struct msm_gpi_tre rx_dma_tre;
	struct msm_gpi_ctrl tx_ev;
	struct msm_gpi_ctrl rx_ev;
	struct dma_async_tx_descriptor *tx_desc;
	struct dma_async_tx_descriptor *rx_desc;
	struct dma_async_tx_descriptor *db_rx_desc;
	struct msm_gpi_dma_async_tx_cb_param tx_cb_param;
	struct msm_gpi_dma_async_tx_cb_param rx_cb_param;
	struct msm_gpi_dma_async_tx_cb_param db_rx_cb_param;
	bool chan_setup;
	struct scatterlist tx_sg[3];
	struct scatterlist rx_sg[3];
	dma_cookie_t tx_cookie;
	dma_cookie_t rx_cookie;
	int num_tx_eot;
	int num_rx_eot;
	bool qup_gsi_err;
	bool qup_gsi_global_err;
};

#endif /* __SPI_Q2SPI_GPI_H_ */
