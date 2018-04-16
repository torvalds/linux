// SPDX-License-Identifier: (GPL-2.0 OR MIT)
// Copyright (c) 2018 Synopsys, Inc. and/or its affiliates.
// stmmac HW Interface Callbacks

#ifndef __STMMAC_HWIF_H__
#define __STMMAC_HWIF_H__

#define stmmac_do_void_callback(__priv, __module, __cname,  __arg0, __args...) \
({ \
	int __result = -EINVAL; \
	if ((__priv)->hw->__module->__cname) { \
		(__priv)->hw->__module->__cname((__arg0), ##__args); \
		__result = 0; \
	} \
	__result; \
})
#define stmmac_do_callback(__priv, __module, __cname,  __arg0, __args...) \
({ \
	int __result = -EINVAL; \
	if ((__priv)->hw->__module->__cname) \
		__result = (__priv)->hw->__module->__cname((__arg0), ##__args); \
	__result; \
})

struct stmmac_extra_stats;
struct stmmac_safety_stats;
struct dma_desc;
struct dma_extended_desc;

/* Descriptors helpers */
struct stmmac_desc_ops {
	/* DMA RX descriptor ring initialization */
	void (*init_rx_desc)(struct dma_desc *p, int disable_rx_ic, int mode,
			int end);
	/* DMA TX descriptor ring initialization */
	void (*init_tx_desc)(struct dma_desc *p, int mode, int end);
	/* Invoked by the xmit function to prepare the tx descriptor */
	void (*prepare_tx_desc)(struct dma_desc *p, int is_fs, int len,
			bool csum_flag, int mode, bool tx_own, bool ls,
			unsigned int tot_pkt_len);
	void (*prepare_tso_tx_desc)(struct dma_desc *p, int is_fs, int len1,
			int len2, bool tx_own, bool ls, unsigned int tcphdrlen,
			unsigned int tcppayloadlen);
	/* Set/get the owner of the descriptor */
	void (*set_tx_owner)(struct dma_desc *p);
	int (*get_tx_owner)(struct dma_desc *p);
	/* Clean the tx descriptor as soon as the tx irq is received */
	void (*release_tx_desc)(struct dma_desc *p, int mode);
	/* Clear interrupt on tx frame completion. When this bit is
	 * set an interrupt happens as soon as the frame is transmitted */
	void (*set_tx_ic)(struct dma_desc *p);
	/* Last tx segment reports the transmit status */
	int (*get_tx_ls)(struct dma_desc *p);
	/* Return the transmit status looking at the TDES1 */
	int (*tx_status)(void *data, struct stmmac_extra_stats *x,
			struct dma_desc *p, void __iomem *ioaddr);
	/* Get the buffer size from the descriptor */
	int (*get_tx_len)(struct dma_desc *p);
	/* Handle extra events on specific interrupts hw dependent */
	void (*set_rx_owner)(struct dma_desc *p);
	/* Get the receive frame size */
	int (*get_rx_frame_len)(struct dma_desc *p, int rx_coe_type);
	/* Return the reception status looking at the RDES1 */
	int (*rx_status)(void *data, struct stmmac_extra_stats *x,
			struct dma_desc *p);
	void (*rx_extended_status)(void *data, struct stmmac_extra_stats *x,
			struct dma_extended_desc *p);
	/* Set tx timestamp enable bit */
	void (*enable_tx_timestamp) (struct dma_desc *p);
	/* get tx timestamp status */
	int (*get_tx_timestamp_status) (struct dma_desc *p);
	/* get timestamp value */
	void (*get_timestamp)(void *desc, u32 ats, u64 *ts);
	/* get rx timestamp status */
	int (*get_rx_timestamp_status)(void *desc, void *next_desc, u32 ats);
	/* Display ring */
	void (*display_ring)(void *head, unsigned int size, bool rx);
	/* set MSS via context descriptor */
	void (*set_mss)(struct dma_desc *p, unsigned int mss);
};

#define stmmac_init_rx_desc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, init_rx_desc, __args)
#define stmmac_init_tx_desc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, init_tx_desc, __args)
#define stmmac_prepare_tx_desc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, prepare_tx_desc, __args)
#define stmmac_prepare_tso_tx_desc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, prepare_tso_tx_desc, __args)
#define stmmac_set_tx_owner(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_tx_owner, __args)
#define stmmac_get_tx_owner(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_tx_owner, __args)
#define stmmac_release_tx_desc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, release_tx_desc, __args)
#define stmmac_set_tx_ic(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_tx_ic, __args)
#define stmmac_get_tx_ls(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_tx_ls, __args)
#define stmmac_tx_status(__priv, __args...) \
	stmmac_do_callback(__priv, desc, tx_status, __args)
#define stmmac_get_tx_len(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_tx_len, __args)
#define stmmac_set_rx_owner(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_rx_owner, __args)
#define stmmac_get_rx_frame_len(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_rx_frame_len, __args)
#define stmmac_rx_status(__priv, __args...) \
	stmmac_do_callback(__priv, desc, rx_status, __args)
#define stmmac_rx_extended_status(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, rx_extended_status, __args)
#define stmmac_enable_tx_timestamp(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, enable_tx_timestamp, __args)
#define stmmac_get_tx_timestamp_status(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_tx_timestamp_status, __args)
#define stmmac_get_timestamp(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, get_timestamp, __args)
#define stmmac_get_rx_timestamp_status(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_rx_timestamp_status, __args)
#define stmmac_display_ring(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, display_ring, __args)
#define stmmac_set_mss(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_mss, __args)

struct stmmac_dma_cfg;
struct dma_features;

/* Specific DMA helpers */
struct stmmac_dma_ops {
	/* DMA core initialization */
	int (*reset)(void __iomem *ioaddr);
	void (*init)(void __iomem *ioaddr, struct stmmac_dma_cfg *dma_cfg,
		     u32 dma_tx, u32 dma_rx, int atds);
	void (*init_chan)(void __iomem *ioaddr,
			  struct stmmac_dma_cfg *dma_cfg, u32 chan);
	void (*init_rx_chan)(void __iomem *ioaddr,
			     struct stmmac_dma_cfg *dma_cfg,
			     u32 dma_rx_phy, u32 chan);
	void (*init_tx_chan)(void __iomem *ioaddr,
			     struct stmmac_dma_cfg *dma_cfg,
			     u32 dma_tx_phy, u32 chan);
	/* Configure the AXI Bus Mode Register */
	void (*axi)(void __iomem *ioaddr, struct stmmac_axi *axi);
	/* Dump DMA registers */
	void (*dump_regs)(void __iomem *ioaddr, u32 *reg_space);
	/* Set tx/rx threshold in the csr6 register
	 * An invalid value enables the store-and-forward mode */
	void (*dma_mode)(void __iomem *ioaddr, int txmode, int rxmode,
			 int rxfifosz);
	void (*dma_rx_mode)(void __iomem *ioaddr, int mode, u32 channel,
			    int fifosz, u8 qmode);
	void (*dma_tx_mode)(void __iomem *ioaddr, int mode, u32 channel,
			    int fifosz, u8 qmode);
	/* To track extra statistic (if supported) */
	void (*dma_diagnostic_fr) (void *data, struct stmmac_extra_stats *x,
				   void __iomem *ioaddr);
	void (*enable_dma_transmission) (void __iomem *ioaddr);
	void (*enable_dma_irq)(void __iomem *ioaddr, u32 chan);
	void (*disable_dma_irq)(void __iomem *ioaddr, u32 chan);
	void (*start_tx)(void __iomem *ioaddr, u32 chan);
	void (*stop_tx)(void __iomem *ioaddr, u32 chan);
	void (*start_rx)(void __iomem *ioaddr, u32 chan);
	void (*stop_rx)(void __iomem *ioaddr, u32 chan);
	int (*dma_interrupt) (void __iomem *ioaddr,
			      struct stmmac_extra_stats *x, u32 chan);
	/* If supported then get the optional core features */
	void (*get_hw_feature)(void __iomem *ioaddr,
			       struct dma_features *dma_cap);
	/* Program the HW RX Watchdog */
	void (*rx_watchdog)(void __iomem *ioaddr, u32 riwt, u32 number_chan);
	void (*set_tx_ring_len)(void __iomem *ioaddr, u32 len, u32 chan);
	void (*set_rx_ring_len)(void __iomem *ioaddr, u32 len, u32 chan);
	void (*set_rx_tail_ptr)(void __iomem *ioaddr, u32 tail_ptr, u32 chan);
	void (*set_tx_tail_ptr)(void __iomem *ioaddr, u32 tail_ptr, u32 chan);
	void (*enable_tso)(void __iomem *ioaddr, bool en, u32 chan);
};

#define stmmac_reset(__priv, __args...) \
	stmmac_do_callback(__priv, dma, reset, __args)
#define stmmac_dma_init(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, init, __args)
#define stmmac_init_chan(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, init_chan, __args)
#define stmmac_init_rx_chan(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, init_rx_chan, __args)
#define stmmac_init_tx_chan(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, init_tx_chan, __args)
#define stmmac_axi(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, axi, __args)
#define stmmac_dump_dma_regs(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, dump_regs, __args)
#define stmmac_dma_mode(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, dma_mode, __args)
#define stmmac_dma_rx_mode(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, dma_rx_mode, __args)
#define stmmac_dma_tx_mode(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, dma_tx_mode, __args)
#define stmmac_dma_diagnostic_fr(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, dma_diagnostic_fr, __args)
#define stmmac_enable_dma_transmission(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, enable_dma_transmission, __args)
#define stmmac_enable_dma_irq(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, enable_dma_irq, __args)
#define stmmac_disable_dma_irq(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, disable_dma_irq, __args)
#define stmmac_start_tx(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, start_tx, __args)
#define stmmac_stop_tx(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, stop_tx, __args)
#define stmmac_start_rx(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, start_rx, __args)
#define stmmac_stop_rx(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, stop_rx, __args)
#define stmmac_dma_interrupt_status(__priv, __args...) \
	stmmac_do_callback(__priv, dma, dma_interrupt, __args)
#define stmmac_get_hw_feature(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, get_hw_feature, __args)
#define stmmac_rx_watchdog(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, rx_watchdog, __args)
#define stmmac_set_tx_ring_len(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, set_tx_ring_len, __args)
#define stmmac_set_rx_ring_len(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, set_rx_ring_len, __args)
#define stmmac_set_rx_tail_ptr(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, set_rx_tail_ptr, __args)
#define stmmac_set_tx_tail_ptr(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, set_tx_tail_ptr, __args)
#define stmmac_enable_tso(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, enable_tso, __args)

#endif /* __STMMAC_HWIF_H__ */
