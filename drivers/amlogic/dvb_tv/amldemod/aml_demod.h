#ifndef AML_DEMOD_H
#define AML_DEMOD_H
#ifndef CONFIG_AM_DEMOD_FPGA_VER
#define CONFIG_AM_DEMOD_FPGA_VER
#endif//CONFIG_AM_DEMOD_FPGA_VER

//#include <linux/types.h>
#define u8_t u8
#define u16_t u16
#define u32_t u32



struct aml_demod_i2c {
    u8_t  tuner;   // type
    u8_t  addr;    // slave addr
    u32_t scl_oe;
    u32_t scl_out;
    u32_t scl_in;
    u8_t  scl_bit;
    u32_t sda_oe;
    u32_t sda_out;
    u32_t sda_in;
    u8_t  sda_bit;
    u8_t  udelay;  // us
    u8_t  retries;
    u8_t  debug;   // 1:debug
    u8_t  tmp;     // spare
    u8_t i2c_id;
    void *i2c_priv;
};

struct aml_tuner_sys {
    u8_t  mode;     //
    u8_t  amp;    //
    u8_t  if_agc_speed;
	u32_t ch_freq;
	u32_t if_freq;
	u32_t rssi;
	u32_t delay;
	u8_t  bandwith;
};



struct aml_demod_sys {
    u8_t  clk_en;     // 1:on
    u8_t  clk_src;    // 2 bits
    u8_t  clk_div;    // 7 bits
    u8_t  pll_n;      // 5 bits
    u16_t pll_m;      // 9 bits
    u8_t  pll_od;     // 7 bits
    u8_t  pll_sys_xd; // 5 bits
    u8_t  pll_adc_xd; // 5 bits
    u8_t  agc_sel;    // pin mux
    u8_t  adc_en;     // 1:on
    u8_t  debug;      // 1:debug
    u32_t i2c;        // pointer
    u32_t adc_clk;
	u32_t demod_clk;
};

struct aml_demod_sts {
    u32_t ch_sts;
    u32_t freq_off;   // Hz
    u32_t ch_pow;
    u32_t ch_snr;
    u32_t ch_ber;
    u32_t ch_per;
    u32_t symb_rate;
    u32_t dat0;
    u32_t dat1;
};

struct aml_demod_sta {
    u8_t  clk_en;     // on/off
    u8_t  adc_en;     // on/off
    u32_t clk_freq;   // kHz
    u32_t adc_freq;   // kHz
    u8_t  dvb_mode;   // dvb-t/c mode
    u8_t  ch_mode;    // 16,32,..,256QAM or 2K,4K,8K
    u8_t  agc_mode;   // if, rf or both.
    u8_t  tuner;      // type
    u32_t ch_freq;    // kHz
    u16_t ch_if;      // kHz
    u16_t ch_bw;      // kHz
    u16_t symb_rate;  // kHz
    u8_t  debug;
    u8_t  tmp;
    u32_t sts;        // pointer
};

struct aml_demod_dvbc {
    u8_t  mode;
    u8_t  tmp;
    u16_t symb_rate;
    u32_t ch_freq;
    u32_t dat0;
    u32_t dat1;
};

struct aml_demod_dvbt {
    u8_t  bw;
    u8_t  sr;
    u8_t  ifreq;
    u8_t  agc_mode;
    u32_t ch_freq;
    u32_t dat0;
    u32_t dat1;
    u32_t layer;

};

struct aml_demod_dtmb {
    u8_t  bw;
    u8_t  sr;
    u8_t  ifreq;
    u8_t  agc_mode;
    u32_t ch_freq;
    u32_t dat0;
    u32_t dat1;
	u32_t mode;

};

struct aml_demod_atsc {
    u8_t  bw;
    u8_t  sr;
    u8_t  ifreq;
    u8_t  agc_mode;
    u32_t ch_freq;
    u32_t dat0;
    u32_t dat1;
	u32_t mode;

};

struct aml_demod_mem {
    u32_t addr;
    u32_t dat;


};

struct aml_cap_data {
	u32_t cap_addr;
	u32_t cap_size;
	u32_t cap_afifo;

};




struct aml_demod_reg {
    u8_t  mode; // 0: dvbc, 1: demod, 2: dvbt, 3:other, 10:i2c
    u8_t  rw;   // 0: read, 1: write.
    u32_t addr;
    u32_t val;
};

struct aml_demod_regs {
    u8_t  mode; // 0: dvbc, 1: demod, 2: dvbt, 3:other, 10:i2c
    u8_t  rw;   // 0: read, 1: write.
    u32_t addr;
    u32_t addr_len;
    u32_t n;
    u32_t vals[1];/*[mode i2c]: write:n*u32_t, read:n*u8_t*/
};
struct fpga_m1_sdio {
	  unsigned long addr;
	  unsigned long byte_count;
	  unsigned char *data_buf;
} ;

#define AML_DEMOD_SET_SYS        _IOW('D',  0, struct aml_demod_sys)
#define AML_DEMOD_GET_SYS        _IOR('D',  1, struct aml_demod_sys)
#define AML_DEMOD_TEST           _IOR('D',  2, u32_t)
#define AML_DEMOD_TURN_ON        _IOR('D',  3, u32_t)
#define AML_DEMOD_TURN_OFF       _IOR('D',  4, u32_t)
#define AML_DEMOD_SET_TUNER      _IOW('D',  5, struct aml_tuner_sys)
#define AML_DEMOD_GET_RSSI       _IOR('D',  6, u32_t)



#define AML_DEMOD_DVBC_SET_CH    _IOW('D', 10, struct aml_demod_dvbc)
#define AML_DEMOD_DVBC_GET_CH    _IOR('D', 11, struct aml_demod_dvbc)
#define AML_DEMOD_DVBC_TEST      _IOR('D', 12, u32_t)

#define AML_DEMOD_DVBT_SET_CH    _IOW('D', 20, struct aml_demod_dvbt)
#define AML_DEMOD_DVBT_GET_CH    _IOR('D', 21, struct aml_demod_dvbt)
#define AML_DEMOD_DVBT_TEST      _IOR('D', 22, u32_t)

#define AML_DEMOD_DTMB_SET_CH    _IOW('D', 50, struct aml_demod_dtmb)
#define AML_DEMOD_DTMB_GET_CH    _IOR('D', 51, struct aml_demod_dtmb)
#define AML_DEMOD_DTMB_TEST      _IOR('D', 52, u32_t)

#define AML_DEMOD_ATSC_SET_CH    _IOW('D', 60, struct aml_demod_atsc)
#define AML_DEMOD_ATSC_GET_CH    _IOR('D', 61, struct aml_demod_atsc)
#define AML_DEMOD_ATSC_TEST      _IOR('D', 62, u32_t)
#define AML_DEMOD_ATSC_IRQ       _IOR('D', 63, u32_t)


#define AML_DEMOD_RESET_MEM		  _IOR('D', 70, u32_t)
#define	AML_DEMOD_READ_MEM		  _IOR('D', 71, u32_t)
#define AML_DEMOD_SET_MEM		  _IOR('D', 72, struct aml_demod_mem)



#define AML_DEMOD_SET_REG        _IOW('D', 30, struct aml_demod_reg)
#define AML_DEMOD_GET_REG        _IOR('D', 31, struct aml_demod_reg)
#define AML_DEMOD_SET_REGS        _IOW('D', 32, struct aml_demod_regs)
#define AML_DEMOD_GET_REGS        _IOR('D', 33, struct aml_demod_regs)
#define FPGA2M1_SDIO_WR_DDR      _IOW('D', 40, struct fpga_m1_sdio)
#define FPGA2M1_SDIO_RD_DDR      _IOR('D', 41, struct fpga_m1_sdio)
#define FPGA2M1_SDIO_INIT        _IO('D', 42)
#define FPGA2M1_SDIO_EXIT        _IO('D', 43)

int read_memory_to_file(struct aml_cap_data *cap);
int read_reg(int addr);
void wait_capture(int cap_cur_addr, int depth_MB, int start);
int cap_adc_data(struct aml_cap_data *cap);




#endif /* AML_DEMOD_H */
