#ifndef __SPICC_H__
#define __SPICC_H__


#define spicc_clk_gate_on()  SET_CBUS_REG_MASK(HHI_GCLK_MPEG0, (1<<8))
#define spicc_clk_gate_off() CLEAR_CBUS_REG_MASK(HHI_GCLK_MPEG0, (1<<8))

#define SPICC_FIFO_SIZE 16

struct spicc_conreg {
  unsigned int enable       :1;
  unsigned int mode         :1;
  unsigned int xch          :1;
  unsigned int smc          :1;
  	#define SPICC_DMA  0
		#define SPICC_PIO  1
  unsigned int clk_pol      :1;
  unsigned int clk_pha      :1;
  unsigned int ss_ctl       :1;
  unsigned int ss_pol       :1;
  unsigned int drctl        :2;
  unsigned int rsv1         :2;
  unsigned int chip_select  :2;
  unsigned int rsv2         :2;
  unsigned int data_rate_div :3;
  unsigned int bits_per_word :6;
  unsigned int burst_len    :7;
};

struct spicc_intreg {
  unsigned int tx_empty_en  :1;
  unsigned int tx_half_en   :1;
  unsigned int tx_full_en   :1;
  unsigned int rx_ready_en  :1;
  unsigned int rx_half_en   :1;
  unsigned int rx_full_en   :1;
  unsigned int rx_of_en     :1;
  unsigned int xfer_com_en  :1;
  unsigned int rsv1         :24;
};

struct spicc_dmareg {
  unsigned int en           :1;
  unsigned int tx_fifo_th   :5;
  unsigned int rx_fifo_th   :5;
  unsigned int num_rd_burst :4;
  unsigned int num_wr_burst :4;
  unsigned int urgent       :1;
  unsigned int thread_id    :6;
  unsigned int burst_num    :6;
};

struct spicc_statreg {
  unsigned int tx_empty     :1;
  unsigned int tx_half      :1;
  unsigned int tx_full      :1;
  unsigned int rx_ready     :1;
  unsigned int rx_half      :1;
  unsigned int rx_full      :1;
  unsigned int rx_of        :1;
  unsigned int xfer_com     :1;
  unsigned int rsv1         :24;
};

struct spicc_regs {
	volatile unsigned int rxdata;
	volatile unsigned int txdata;
	volatile struct spicc_conreg conreg;
	volatile struct spicc_intreg intreg;
	volatile struct spicc_dmareg dmareg;
	volatile struct spicc_statreg statreg;
	volatile unsigned int periodreg;
	volatile unsigned int testreg;
	volatile unsigned int draddr;
	volatile unsigned int dwaddr;
};

struct spicc_platform_data {
  int device_id;
  struct spicc_regs __iomem *regs;
#ifdef CONFIG_OF
	struct pinctrl *pinctrl;
#else
	pinmux_set_t pinctrl;
#endif
  int num_chipselect;
  int *cs_gpios;
};


#endif

