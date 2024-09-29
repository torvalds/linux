/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/drivers/mmc/host/mmci.h - ARM PrimeCell MMCI PL180/1 driver
 *
 *  Copyright (C) 2003 Deep Blue Solutions, Ltd, All Rights Reserved.
 */
#define MMCIPOWER		0x000
#define MCI_PWR_OFF		0x00
#define MCI_PWR_UP		0x02
#define MCI_PWR_ON		0x03
#define MCI_OD			(1 << 6)
#define MCI_ROD			(1 << 7)
/*
 * The ST Micro version does not have ROD and reuse the voltage registers for
 * direction settings.
 */
#define MCI_ST_DATA2DIREN	(1 << 2)
#define MCI_ST_CMDDIREN		(1 << 3)
#define MCI_ST_DATA0DIREN	(1 << 4)
#define MCI_ST_DATA31DIREN	(1 << 5)
#define MCI_ST_FBCLKEN		(1 << 7)
#define MCI_ST_DATA74DIREN	(1 << 8)
/*
 * The STM32 sdmmc does not have PWR_UP/OD/ROD
 * and uses the power register for
 */
#define MCI_STM32_PWR_CYC	0x02
#define MCI_STM32_VSWITCH	BIT(2)
#define MCI_STM32_VSWITCHEN	BIT(3)
#define MCI_STM32_DIRPOL	BIT(4)

#define MMCICLOCK		0x004
#define MCI_CLK_ENABLE		(1 << 8)
#define MCI_CLK_PWRSAVE		(1 << 9)
#define MCI_CLK_BYPASS		(1 << 10)
#define MCI_4BIT_BUS		(1 << 11)
/*
 * 8bit wide buses, hardware flow contronl, negative edges and clock inversion
 * supported in ST Micro U300 and Ux500 versions
 */
#define MCI_ST_8BIT_BUS		(1 << 12)
#define MCI_ST_U300_HWFCEN	(1 << 13)
#define MCI_ST_UX500_NEG_EDGE	(1 << 13)
#define MCI_ST_UX500_HWFCEN	(1 << 14)
#define MCI_ST_UX500_CLK_INV	(1 << 15)
/* Modified PL180 on Versatile Express platform */
#define MCI_ARM_HWFCEN		(1 << 12)

/* Modified on Qualcomm Integrations */
#define MCI_QCOM_CLK_WIDEBUS_8	(BIT(10) | BIT(11))
#define MCI_QCOM_CLK_FLOWENA	BIT(12)
#define MCI_QCOM_CLK_INVERTOUT	BIT(13)

/* select in latch data and command in */
#define MCI_QCOM_CLK_SELECT_IN_FBCLK	BIT(15)
#define MCI_QCOM_CLK_SELECT_IN_DDR_MODE	(BIT(14) | BIT(15))

/* Modified on STM32 sdmmc */
#define MCI_STM32_CLK_CLKDIV_MSK	GENMASK(9, 0)
#define MCI_STM32_CLK_WIDEBUS_4		BIT(14)
#define MCI_STM32_CLK_WIDEBUS_8		BIT(15)
#define MCI_STM32_CLK_NEGEDGE		BIT(16)
#define MCI_STM32_CLK_HWFCEN		BIT(17)
#define MCI_STM32_CLK_DDR		BIT(18)
#define MCI_STM32_CLK_BUSSPEED		BIT(19)
#define MCI_STM32_CLK_SEL_MSK		GENMASK(21, 20)
#define MCI_STM32_CLK_SELCK		(0 << 20)
#define MCI_STM32_CLK_SELCKIN		(1 << 20)
#define MCI_STM32_CLK_SELFBCK		(2 << 20)

#define MMCIARGUMENT		0x008

/* The command register controls the Command Path State Machine (CPSM) */
#define MMCICOMMAND		0x00c
#define MCI_CPSM_RESPONSE	BIT(6)
#define MCI_CPSM_LONGRSP	BIT(7)
#define MCI_CPSM_INTERRUPT	BIT(8)
#define MCI_CPSM_PENDING	BIT(9)
#define MCI_CPSM_ENABLE		BIT(10)
/* Command register flag extensions in the ST Micro versions */
#define MCI_CPSM_ST_SDIO_SUSP		BIT(11)
#define MCI_CPSM_ST_ENCMD_COMPL		BIT(12)
#define MCI_CPSM_ST_NIEN		BIT(13)
#define MCI_CPSM_ST_CE_ATACMD		BIT(14)
/* Command register flag extensions in the Qualcomm versions */
#define MCI_CPSM_QCOM_PROGENA		BIT(11)
#define MCI_CPSM_QCOM_DATCMD		BIT(12)
#define MCI_CPSM_QCOM_MCIABORT		BIT(13)
#define MCI_CPSM_QCOM_CCSENABLE		BIT(14)
#define MCI_CPSM_QCOM_CCSDISABLE	BIT(15)
#define MCI_CPSM_QCOM_AUTO_CMD19	BIT(16)
#define MCI_CPSM_QCOM_AUTO_CMD21	BIT(21)
/* Command register in STM32 sdmmc versions */
#define MCI_CPSM_STM32_CMDTRANS		BIT(6)
#define MCI_CPSM_STM32_CMDSTOP		BIT(7)
#define MCI_CPSM_STM32_WAITRESP_MASK	GENMASK(9, 8)
#define MCI_CPSM_STM32_NORSP		(0 << 8)
#define MCI_CPSM_STM32_SRSP_CRC		(1 << 8)
#define MCI_CPSM_STM32_SRSP		(2 << 8)
#define MCI_CPSM_STM32_LRSP_CRC		(3 << 8)
#define MCI_CPSM_STM32_ENABLE		BIT(12)

#define MMCIRESPCMD		0x010
#define MMCIRESPONSE0		0x014
#define MMCIRESPONSE1		0x018
#define MMCIRESPONSE2		0x01c
#define MMCIRESPONSE3		0x020
#define MMCIDATATIMER		0x024
#define MMCIDATALENGTH		0x028

/* The data control register controls the Data Path State Machine (DPSM) */
#define MMCIDATACTRL		0x02c
#define MCI_DPSM_ENABLE		BIT(0)
#define MCI_DPSM_DIRECTION	BIT(1)
#define MCI_DPSM_MODE		BIT(2)
#define MCI_DPSM_DMAENABLE	BIT(3)
#define MCI_DPSM_BLOCKSIZE	BIT(4)
/* Control register extensions in the ST Micro U300 and Ux500 versions */
#define MCI_DPSM_ST_RWSTART	BIT(8)
#define MCI_DPSM_ST_RWSTOP	BIT(9)
#define MCI_DPSM_ST_RWMOD	BIT(10)
#define MCI_DPSM_ST_SDIOEN	BIT(11)
/* Control register extensions in the ST Micro Ux500 versions */
#define MCI_DPSM_ST_DMAREQCTL	BIT(12)
#define MCI_DPSM_ST_DBOOTMODEEN	BIT(13)
#define MCI_DPSM_ST_BUSYMODE	BIT(14)
#define MCI_DPSM_ST_DDRMODE	BIT(15)
/* Control register extensions in the Qualcomm versions */
#define MCI_DPSM_QCOM_DATA_PEND	BIT(17)
#define MCI_DPSM_QCOM_RX_DATA_PEND BIT(20)
/* Control register extensions in STM32 versions */
#define MCI_DPSM_STM32_MODE_BLOCK	(0 << 2)
#define MCI_DPSM_STM32_MODE_SDIO	(1 << 2)
#define MCI_DPSM_STM32_MODE_STREAM	(2 << 2)
#define MCI_DPSM_STM32_MODE_BLOCK_STOP	(3 << 2)

#define MMCIDATACNT		0x030
#define MMCISTATUS		0x034
#define MCI_CMDCRCFAIL		(1 << 0)
#define MCI_DATACRCFAIL		(1 << 1)
#define MCI_CMDTIMEOUT		(1 << 2)
#define MCI_DATATIMEOUT		(1 << 3)
#define MCI_TXUNDERRUN		(1 << 4)
#define MCI_RXOVERRUN		(1 << 5)
#define MCI_CMDRESPEND		(1 << 6)
#define MCI_CMDSENT		(1 << 7)
#define MCI_DATAEND		(1 << 8)
#define MCI_STARTBITERR		(1 << 9)
#define MCI_DATABLOCKEND	(1 << 10)
#define MCI_CMDACTIVE		(1 << 11)
#define MCI_TXACTIVE		(1 << 12)
#define MCI_RXACTIVE		(1 << 13)
#define MCI_TXFIFOHALFEMPTY	(1 << 14)
#define MCI_RXFIFOHALFFULL	(1 << 15)
#define MCI_TXFIFOFULL		(1 << 16)
#define MCI_RXFIFOFULL		(1 << 17)
#define MCI_TXFIFOEMPTY		(1 << 18)
#define MCI_RXFIFOEMPTY		(1 << 19)
#define MCI_TXDATAAVLBL		(1 << 20)
#define MCI_RXDATAAVLBL		(1 << 21)
/* Extended status bits for the ST Micro variants */
#define MCI_ST_SDIOIT		(1 << 22)
#define MCI_ST_CEATAEND		(1 << 23)
#define MCI_ST_CARDBUSY		(1 << 24)
/* Extended status bits for the STM32 variants */
#define MCI_STM32_BUSYD0	BIT(20)
#define MCI_STM32_BUSYD0END	BIT(21)
#define MCI_STM32_VSWEND	BIT(25)

#define MMCICLEAR		0x038
#define MCI_CMDCRCFAILCLR	(1 << 0)
#define MCI_DATACRCFAILCLR	(1 << 1)
#define MCI_CMDTIMEOUTCLR	(1 << 2)
#define MCI_DATATIMEOUTCLR	(1 << 3)
#define MCI_TXUNDERRUNCLR	(1 << 4)
#define MCI_RXOVERRUNCLR	(1 << 5)
#define MCI_CMDRESPENDCLR	(1 << 6)
#define MCI_CMDSENTCLR		(1 << 7)
#define MCI_DATAENDCLR		(1 << 8)
#define MCI_STARTBITERRCLR	(1 << 9)
#define MCI_DATABLOCKENDCLR	(1 << 10)
/* Extended status bits for the ST Micro variants */
#define MCI_ST_SDIOITC		(1 << 22)
#define MCI_ST_CEATAENDC	(1 << 23)
#define MCI_ST_BUSYENDC		(1 << 24)
/* Extended clear bits for the STM32 variants */
#define MCI_STM32_VSWENDC	BIT(25)
#define MCI_STM32_CKSTOPC	BIT(26)

#define MMCIMASK0		0x03c
#define MCI_CMDCRCFAILMASK	(1 << 0)
#define MCI_DATACRCFAILMASK	(1 << 1)
#define MCI_CMDTIMEOUTMASK	(1 << 2)
#define MCI_DATATIMEOUTMASK	(1 << 3)
#define MCI_TXUNDERRUNMASK	(1 << 4)
#define MCI_RXOVERRUNMASK	(1 << 5)
#define MCI_CMDRESPENDMASK	(1 << 6)
#define MCI_CMDSENTMASK		(1 << 7)
#define MCI_DATAENDMASK		(1 << 8)
#define MCI_STARTBITERRMASK	(1 << 9)
#define MCI_DATABLOCKENDMASK	(1 << 10)
#define MCI_CMDACTIVEMASK	(1 << 11)
#define MCI_TXACTIVEMASK	(1 << 12)
#define MCI_RXACTIVEMASK	(1 << 13)
#define MCI_TXFIFOHALFEMPTYMASK	(1 << 14)
#define MCI_RXFIFOHALFFULLMASK	(1 << 15)
#define MCI_TXFIFOFULLMASK	(1 << 16)
#define MCI_RXFIFOFULLMASK	(1 << 17)
#define MCI_TXFIFOEMPTYMASK	(1 << 18)
#define MCI_RXFIFOEMPTYMASK	(1 << 19)
#define MCI_TXDATAAVLBLMASK	(1 << 20)
#define MCI_RXDATAAVLBLMASK	(1 << 21)
/* Extended status bits for the ST Micro variants */
#define MCI_ST_SDIOITMASK	(1 << 22)
#define MCI_ST_CEATAENDMASK	(1 << 23)
#define MCI_ST_BUSYENDMASK	(1 << 24)
/* Extended status bits for the STM32 variants */
#define MCI_STM32_BUSYD0ENDMASK	BIT(21)

#define MMCIMASK1		0x040

/* STM32 sdmmc data FIFO threshold register */
#define MMCI_STM32_FIFOTHRR	0x044
#define MMCI_STM32_THR_MASK	GENMASK(3, 0)

#define MMCIFIFOCNT		0x048
#define MMCIFIFO		0x080 /* to 0x0bc */

/* STM32 sdmmc registers for IDMA (Internal DMA) */
#define MMCI_STM32_IDMACTRLR	0x050
#define MMCI_STM32_IDMAEN	BIT(0)
#define MMCI_STM32_IDMALLIEN	BIT(1)

#define MMCI_STM32_IDMABSIZER		0x054

#define MMCI_STM32_IDMABASE0R	0x058

#define MMCI_STM32_IDMALAR	0x64
#define MMCI_STM32_IDMALA_MASK	GENMASK(13, 0)
#define MMCI_STM32_ABR		BIT(29)
#define MMCI_STM32_ULS		BIT(30)
#define MMCI_STM32_ULA		BIT(31)

#define MMCI_STM32_IDMABAR	0x68

#define MCI_IRQENABLE	\
	(MCI_CMDCRCFAILMASK | MCI_DATACRCFAILMASK | MCI_CMDTIMEOUTMASK | \
	MCI_DATATIMEOUTMASK | MCI_TXUNDERRUNMASK | MCI_RXOVERRUNMASK |	\
	MCI_CMDRESPENDMASK | MCI_CMDSENTMASK)

/* These interrupts are directed to IRQ1 when two IRQ lines are available */
#define MCI_IRQ_PIO_MASK \
	(MCI_RXFIFOHALFFULLMASK | MCI_RXDATAAVLBLMASK | \
	 MCI_TXFIFOHALFEMPTYMASK)

#define MCI_IRQ_PIO_STM32_MASK \
	(MCI_RXFIFOHALFFULLMASK | MCI_TXFIFOHALFEMPTYMASK)

#define NR_SG		128

#define MMCI_PINCTRL_STATE_OPENDRAIN "opendrain"

struct clk;
struct dma_chan;
struct mmci_host;

/**
 * enum mmci_busy_state - enumerate the busy detect wait states
 *
 * This is used for the state machine waiting for different busy detect
 * interrupts on hardware that fire a single IRQ for start and end of
 * the busy detect phase on DAT0.
 */
enum mmci_busy_state {
	MMCI_BUSY_WAITING_FOR_START_IRQ,
	MMCI_BUSY_WAITING_FOR_END_IRQ,
	MMCI_BUSY_DONE,
};

/**
 * struct variant_data - MMCI variant-specific quirks
 * @clkreg: default value for MCICLOCK register
 * @clkreg_enable: enable value for MMCICLOCK register
 * @clkreg_8bit_bus_enable: enable value for 8 bit bus
 * @clkreg_neg_edge_enable: enable value for inverted data/cmd output
 * @cmdreg_cpsm_enable: enable value for CPSM
 * @cmdreg_lrsp_crc: enable value for long response with crc
 * @cmdreg_srsp_crc: enable value for short response with crc
 * @cmdreg_srsp: enable value for short response without crc
 * @cmdreg_stop: enable value for stop and abort transmission
 * @datalength_bits: number of bits in the MMCIDATALENGTH register
 * @fifosize: number of bytes that can be written when MMCI_TXFIFOEMPTY
 *	      is asserted (likewise for RX)
 * @fifohalfsize: number of bytes that can be written when MCI_TXFIFOHALFEMPTY
 *		  is asserted (likewise for RX)
 * @data_cmd_enable: enable value for data commands.
 * @st_sdio: enable ST specific SDIO logic
 * @st_clkdiv: true if using a ST-specific clock divider algorithm
 * @stm32_clkdiv: true if using a STM32-specific clock divider algorithm
 * @datactrl_mask_ddrmode: ddr mode mask in datactrl register.
 * @datactrl_mask_sdio: SDIO enable mask in datactrl register
 * @datactrl_blocksz: block size in power of two
 * @datactrl_any_blocksz: true if block any block sizes are accepted by
 *		  hardware, such as with some SDIO traffic that send
 *		  odd packets.
 * @dma_power_of_2: DMA only works with blocks that are a power of 2.
 * @datactrl_first: true if data must be setup before send command
 * @datacnt_useless: true if you could not use datacnt register to read
 *		     remaining data
 * @pwrreg_powerup: power up value for MMCIPOWER register
 * @f_max: maximum clk frequency supported by the controller.
 * @signal_direction: input/out direction of bus signals can be indicated
 * @pwrreg_clkgate: MMCIPOWER register must be used to gate the clock
 * @busy_detect: true if the variant supports busy detection on DAT0.
 * @busy_timeout: true if the variant starts data timer when the DPSM
 *		  enter in Wait_R or Busy state.
 * @busy_dpsm_flag: bitmask enabling busy detection in the DPSM
 * @busy_detect_flag: bitmask identifying the bit in the MMCISTATUS register
 *		      indicating that the card is busy
 * @busy_detect_mask: bitmask identifying the bit in the MMCIMASK0 to mask for
 *		      getting busy end detection interrupts
 * @pwrreg_nopower: bits in MMCIPOWER don't controls ext. power supply
 * @explicit_mclk_control: enable explicit mclk control in driver.
 * @qcom_fifo: enables qcom specific fifo pio read logic.
 * @qcom_dml: enables qcom specific dma glue for dma transfers.
 * @reversed_irq_handling: handle data irq before cmd irq.
 * @mmcimask1: true if variant have a MMCIMASK1 register.
 * @irq_pio_mask: bitmask used to manage interrupt pio transfert in mmcimask
 *		  register
 * @start_err: bitmask identifying the STARTBITERR bit inside MMCISTATUS
 *	       register.
 * @opendrain: bitmask identifying the OPENDRAIN bit inside MMCIPOWER register
 * @dma_lli: true if variant has dma link list feature.
 * @supports_sdio_irq: allow SD I/O card to interrupt the host
 * @stm32_idmabsize_mask: stm32 sdmmc idma buffer size.
 * @dma_flow_controller: use peripheral as flow controller for DMA.
 */
struct variant_data {
	unsigned int		clkreg;
	unsigned int		clkreg_enable;
	unsigned int		clkreg_8bit_bus_enable;
	unsigned int		clkreg_neg_edge_enable;
	unsigned int		cmdreg_cpsm_enable;
	unsigned int		cmdreg_lrsp_crc;
	unsigned int		cmdreg_srsp_crc;
	unsigned int		cmdreg_srsp;
	unsigned int		cmdreg_stop;
	unsigned int		datalength_bits;
	unsigned int		fifosize;
	unsigned int		fifohalfsize;
	unsigned int		data_cmd_enable;
	unsigned int		datactrl_mask_ddrmode;
	unsigned int		datactrl_mask_sdio;
	unsigned int		datactrl_blocksz;
	u8			datactrl_any_blocksz:1;
	u8			dma_power_of_2:1;
	u8			datactrl_first:1;
	u8			datacnt_useless:1;
	u8			st_sdio:1;
	u8			st_clkdiv:1;
	u8			stm32_clkdiv:1;
	u32			pwrreg_powerup;
	u32			f_max;
	u8			signal_direction:1;
	u8			pwrreg_clkgate:1;
	u8			busy_detect:1;
	u8			busy_timeout:1;
	u32			busy_dpsm_flag;
	u32			busy_detect_flag;
	u32			busy_detect_mask;
	u8			pwrreg_nopower:1;
	u8			explicit_mclk_control:1;
	u8			qcom_fifo:1;
	u8			qcom_dml:1;
	u8			reversed_irq_handling:1;
	u8			mmcimask1:1;
	unsigned int		irq_pio_mask;
	u32			start_err;
	u32			opendrain;
	u8			dma_lli:1;
	bool			supports_sdio_irq;
	u32			stm32_idmabsize_mask;
	u32			stm32_idmabsize_align;
	bool			dma_flow_controller;
	void (*init)(struct mmci_host *host);
};

/* mmci variant callbacks */
struct mmci_host_ops {
	int (*validate_data)(struct mmci_host *host, struct mmc_data *data);
	int (*prep_data)(struct mmci_host *host, struct mmc_data *data,
			 bool next);
	void (*unprep_data)(struct mmci_host *host, struct mmc_data *data,
			    int err);
	u32 (*get_datactrl_cfg)(struct mmci_host *host);
	void (*get_next_data)(struct mmci_host *host, struct mmc_data *data);
	int (*dma_setup)(struct mmci_host *host);
	void (*dma_release)(struct mmci_host *host);
	int (*dma_start)(struct mmci_host *host, unsigned int *datactrl);
	void (*dma_finalize)(struct mmci_host *host, struct mmc_data *data);
	void (*dma_error)(struct mmci_host *host);
	void (*set_clkreg)(struct mmci_host *host, unsigned int desired);
	void (*set_pwrreg)(struct mmci_host *host, unsigned int pwr);
	bool (*busy_complete)(struct mmci_host *host, struct mmc_command *cmd, u32 status, u32 err_msk);
	void (*pre_sig_volt_switch)(struct mmci_host *host);
	int (*post_sig_volt_switch)(struct mmci_host *host, struct mmc_ios *ios);
};

struct mmci_host {
	phys_addr_t		phybase;
	void __iomem		*base;
	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_command	stop_abort;
	struct mmc_data		*data;
	struct mmc_host		*mmc;
	struct clk		*clk;
	u8			singleirq:1;

	struct reset_control	*rst;

	spinlock_t		lock;

	unsigned int		mclk;
	/* cached value of requested clk in set_ios */
	unsigned int		clock_cache;
	unsigned int		cclk;
	u32			pwr_reg;
	u32			pwr_reg_add;
	u32			clk_reg;
	u32			clk_reg_add;
	u32			datactrl_reg;
	enum mmci_busy_state	busy_state;
	u32			busy_status;
	u32			mask1_reg;
	u8			vqmmc_enabled:1;
	struct mmci_platform_data *plat;
	struct mmc_host_ops	*mmc_ops;
	struct mmci_host_ops	*ops;
	struct variant_data	*variant;
	void			*variant_priv;
	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_opendrain;

	u8			hw_designer;
	u8			hw_revision:4;

	struct timer_list	timer;
	unsigned int		oldstat;
	u32			irq_action;

	/* pio stuff */
	struct sg_mapping_iter	sg_miter;
	unsigned int		size;
	int (*get_rx_fifocnt)(struct mmci_host *h, u32 status, int remain);

	u8			use_dma:1;
	u8			dma_in_progress:1;
	void			*dma_priv;

	s32			next_cookie;
	struct delayed_work	ux500_busy_timeout_work;
};

#define dma_inprogress(host)	((host)->dma_in_progress)

void mmci_write_clkreg(struct mmci_host *host, u32 clk);
void mmci_write_pwrreg(struct mmci_host *host, u32 pwr);

static inline u32 mmci_dctrl_blksz(struct mmci_host *host)
{
	return (ffs(host->data->blksz) - 1) << 4;
}

#ifdef CONFIG_DMA_ENGINE
int mmci_dmae_prep_data(struct mmci_host *host, struct mmc_data *data,
			bool next);
void mmci_dmae_unprep_data(struct mmci_host *host, struct mmc_data *data,
			   int err);
void mmci_dmae_get_next_data(struct mmci_host *host, struct mmc_data *data);
int mmci_dmae_setup(struct mmci_host *host);
void mmci_dmae_release(struct mmci_host *host);
int mmci_dmae_start(struct mmci_host *host, unsigned int *datactrl);
void mmci_dmae_finalize(struct mmci_host *host, struct mmc_data *data);
void mmci_dmae_error(struct mmci_host *host);
#endif

#ifdef CONFIG_MMC_QCOM_DML
void qcom_variant_init(struct mmci_host *host);
#else
static inline void qcom_variant_init(struct mmci_host *host) {}
#endif

#ifdef CONFIG_MMC_STM32_SDMMC
void sdmmc_variant_init(struct mmci_host *host);
#else
static inline void sdmmc_variant_init(struct mmci_host *host) {}
#endif
