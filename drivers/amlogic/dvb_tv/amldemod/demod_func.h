
#ifdef DEMOD_FUNC_H
#else
#define DEMOD_FUNC_H

#include <linux/types.h>
#include <mach/am_regs.h>
/*#include <mach/register.h>
#include <mach/avosstyle_io.h>
#include <mach/io.h>*/
#include "aml_demod.h"
#include "../aml_fe.h"
#include "amlfrontend.h"


//#define DEMOD_BASE     APB_REG_ADDR(0x20000)
#define DEMOD_BASE APB_REG_ADDR(0x20000)

//#define DEMOD_BASE 0xc8020000
#define DTMB_BASE  (DEMOD_BASE+0x000)
#define DVBT_BASE  (DEMOD_BASE+0x000)
#define ISDBT_BASE (DEMOD_BASE+0x000)
#define QAM_BASE   (DEMOD_BASE+0x400)
#define ATSC_BASE  (DEMOD_BASE+0x800)
#define DEMOD_CFG_BASE  (DEMOD_BASE+0xC00)

#define ADC_REG1_VALUE		 0x003b0232
#define ADC_REG2_VALUE		 0x814d3928
#define ADC_REG3_VALUE		 0x6b425012
#define ADC_REG4_VALUE		 0x101
#define ADC_REG4_CRY_VALUE 0x301
#define ADC_REG5_VALUE		 0x70b
#define ADC_REG6_VALUE		 0x713

#define ADC_REG1	 0x10aa
#define ADC_REG2	 0x10ab
#define ADC_REG3	 0x10ac
#define ADC_REG4	 0x10ad
#define ADC_REG5	 0x1073
#define ADC_REG6	 0x1074


#define DEMOD_REG1_VALUE		 0x0000d007
#define DEMOD_REG2_VALUE		 0x2e805400
#define DEMOD_REG3_VALUE		 0x201


#define DEMOD_REG1		 0xc00
#define DEMOD_REG2		 0xc04
#define DEMOD_REG3		 0xc08



#define Wr(addr, data)   WRITE_CBUS_REG(addr, data) /**(volatile unsigned long *)(0xc1100000|(addr<<2))=data*/
#define Rd(addr)             READ_CBUS_REG(addr)            /**(volatile unsigned long *)(0xc1100000|(addr<<2))*/
// i2c functions
//int aml_i2c_sw_test_bus(struct aml_demod_i2c *adap, char *name);
int am_demod_i2c_xfer(struct aml_demod_i2c *adap, struct i2c_msg *msgs, int num);
int init_tuner_fj2207(struct aml_demod_sta *demod_sta,
		      struct aml_demod_i2c *adap);
int set_tuner_fj2207(struct aml_demod_sta *demod_sta,
		     struct aml_demod_i2c *adap);

int get_fj2207_ch_power(void);
int tuner_get_ch_power(struct aml_fe_dev *adap);
int tda18273_tuner_set_frequncy(unsigned int dwFrequency,unsigned int dwStandard);


int tuner_set_ch (struct aml_demod_sta *demod_sta,
		  struct aml_demod_i2c *demod_i2c);
int demod_set_tuner(struct aml_demod_sta *demod_sta,
		  struct aml_demod_i2c *demod_i2c,
		  struct aml_tuner_sys *tuner_sys);


//dvbt
int dvbt_set_ch(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c,
		struct aml_demod_dvbt *demod_dvbt);

struct demod_status_ops {
	int (*get_status)(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c);
	int (*get_ber)(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c);
	int (*get_snr)(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c);
	int (*get_strength)(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c);
	int (*get_ucblocks)(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c);
};

struct demod_status_ops* dvbt_get_status_ops(void);

//dvbc

int dvbc_set_ch(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c,
		struct aml_demod_dvbc *demod_dvbc);
int dvbc_status(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c,
		struct aml_demod_sts *demod_sts);
int dvbc_isr_islock(void);
void dvbc_isr(struct aml_demod_sta *demod_sta);
u32 dvbc_set_qam_mode(unsigned char mode);
u32 dvbc_get_status(void);
u32 dvbc_set_auto_symtrack(void);
int  dvbc_timer_init(void);
void  dvbc_timer_exit(void);
void dvbc_cci_task(void);
int dvbc_get_cci_task(void);
void dvbc_create_cci_task(void);
void dvbc_kill_cci_task(void);









//atsc

int atsc_set_ch(struct aml_demod_sta *demod_sta,
			struct aml_demod_i2c *demod_i2c,
			struct aml_demod_atsc *demod_atsc);
int check_atsc_fsm_status(void);

void atsc_write_reg(int reg_addr, int reg_data);

unsigned long atsc_read_reg(int reg_addr);

unsigned long atsc_read_iqr_reg(void);

int atsc_qam_set(fe_modulation_t mode);


void qam_initial(int qam_id) ;


//dtmb

int dtmb_set_ch(struct aml_demod_sta *demod_sta,
			struct aml_demod_i2c *demod_i2c,
			struct aml_demod_dtmb *demod_atsc);

void dtmb_reset(void);

int dtmb_read_snr(void);

void dtmb_write_reg(int reg_addr, int reg_data);
unsigned int dtmb_read_reg(int reg_addr);
void dtmb_register_reset(void);







// demod functions
void apb_write_reg(int reg, int val);
unsigned long  apb_read_reg (int reg);
int app_apb_write_reg(int addr,int data);
int app_apb_read_reg(int addr);

void demod_set_cbus_reg(int data, int addr);
unsigned long demod_read_cbus_reg(int addr);
void demod_set_demod_reg(unsigned long data, unsigned long addr);
unsigned long demod_read_demod_reg(unsigned long addr);












void ofdm_initial(
    int bandwidth, // 00:8M 01:7M 10:6M 11:5M
    int samplerate,// 00:45M 01:20.8333M 10:20.7M 11:28.57
    int IF,        // 000:36.13M 001:-5.5M 010:4.57M 011:4M 100:5M
    int mode,       // 00:DVBT,01:ISDBT
    int tc_mode     // 0: Unsigned, 1:TC
) ;

void monitor_isdbt(void);
void demod_set_reg(struct aml_demod_reg *demod_reg);
void demod_get_reg(struct aml_demod_reg *demod_reg);


//void demod_calc_clk(struct aml_demod_sta *demod_sta);
int demod_set_sys(struct aml_demod_sta *demod_sta,
		  struct aml_demod_i2c *demod_i2c,
		  struct aml_demod_sys *demod_sys);
//int demod_get_sys(struct aml_demod_i2c *demod_i2c,
//		  struct aml_demod_sys *demod_sys);
//int dvbt_set_ch(struct aml_demod_sta *demod_sta,
//		struct aml_demod_i2c *demod_i2c,
//		struct aml_demod_dvbt *demod_dvbt);
//int tuner_set_ch (struct aml_demod_sta *demod_sta,
//		  struct aml_demod_i2c *demod_i2c);

//typedef char               int8_t;
//typedef short int          int16_t;
//typedef int                int32_t;
//typedef long               int64_t;
/*typedef unsigned char      uint8_t;
typedef unsigned short int uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long      uint64_t;*/

/*typedef unsigned   char    u8_t;
typedef signed     char    s8_t;
typedef unsigned   short   u16_t;
typedef signed     short   s16_t;
typedef unsigned   int     u32_t;
typedef signed     int     s32_t;
typedef unsigned   long    u64_t;
typedef signed     long    s64_t;*/


//#define extadc


typedef union demod_dig_clk {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned demod_clk_div:7;
        unsigned reserved0:1;
        unsigned demod_clk_en:1;
        unsigned demod_clk_sel:2;
        unsigned reserved1:5;
        unsigned adc_extclk_div:7;  // 34
        unsigned use_adc_extclk:1;   // 1
        unsigned adc_extclk_en:1;   //  1
        unsigned adc_extclk_sel:3;    //   1
        unsigned reserved2:4;
    } b;
} demod_dig_clk_t;

typedef union demod_adc_clk {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned pll_m:9;
        unsigned pll_n:5;
        unsigned pll_od:2;
        unsigned pll_xd:5;
        unsigned reserved0:3;
        unsigned pll_ss_clk:4;
        unsigned pll_ss_en:1;
        unsigned reset:1;
        unsigned pll_pd:1;
        unsigned reserved1:1;
    } b;
} demod_adc_clk_t;

typedef struct demod_cfg_regs {
    volatile uint32_t cfg0;
    volatile uint32_t cfg1;
    volatile uint32_t cfg2;
    volatile uint32_t cfg3;
    volatile uint32_t info0;
    volatile uint32_t info1;
} demod_cfg_regs_t;

typedef union demod_cfg0 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned mode:4;
        unsigned ts_sel:4;
        unsigned test_bus_clk:1;
        unsigned adc_ext:1;
        unsigned adc_rvs:1;
        unsigned adc_swap:1;
        unsigned adc_format:1;
        unsigned adc_regout:1;
        unsigned adc_regsel:1;
        unsigned adc_regadj:5;
        unsigned adc_value:10;
        unsigned adc_test:1;
        unsigned ddr_sel:1;
    } b;
} demod_cfg0_t;

typedef union demod_cfg1 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved:8;
        unsigned ref_top:2;
        unsigned ref_bot:2;
        unsigned cml_xs:2;
        unsigned cml_1s:2;
        unsigned vdda_sel:2;
        unsigned bias_sel_sha:2;
        unsigned bias_sel_mdac2:2;
        unsigned bias_sel_mdac1:2;
        unsigned fast_chg:1;
        unsigned rin_sel:3;
        unsigned en_ext_vbg:1;
        unsigned en_cmlgen_res:1;
        unsigned en_ext_vdd12:1;
        unsigned en_ext_ref:1;
    } b;
} demod_cfg1_t;

typedef union demod_cfg2 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned en_adc:1;
        unsigned biasgen_ibipt_sel:2;
        unsigned biasgen_ibic_sel:2;
        unsigned biasgen_rsv:4;
        unsigned biasgen_en:1;
        unsigned biasgen_bias_sel_adc:2;
        unsigned biasgen_bias_sel_cml1:2;
        unsigned biasgen_bias_sel_ref_op:2;
        unsigned clk_phase_sel:1;
        unsigned reserved:15;
    } b;
} demod_cfg2_t;

typedef union demod_cfg3 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned dc_arb_mask:3;
        unsigned dc_arb_enable:1;
        unsigned reserved:28;
    } b;
} demod_cfg3_t;

typedef struct dtmb_cfg_regs {
    volatile uint32_t dtmb_cfg_00;
    volatile uint32_t dtmb_cfg_01;
    volatile uint32_t dtmb_cfg_02;
    volatile uint32_t dtmb_cfg_03;
    volatile uint32_t dtmb_cfg_04;
    volatile uint32_t dtmb_cfg_05;
    volatile uint32_t dtmb_cfg_06;
    volatile uint32_t dtmb_cfg_07;
    volatile uint32_t dtmb_cfg_08;
    volatile uint32_t dtmb_cfg_09;
    volatile uint32_t dtmb_cfg_0a;
    volatile uint32_t dtmb_cfg_0b;
    volatile uint32_t dtmb_cfg_0c;
    volatile uint32_t dtmb_cfg_0d;
    volatile uint32_t dtmb_cfg_0e;
    volatile uint32_t dtmb_cfg_0f;
    volatile uint32_t dtmb_cfg_10;
    volatile uint32_t dtmb_cfg_11;
    volatile uint32_t dtmb_cfg_12;
    volatile uint32_t dtmb_cfg_13;
    volatile uint32_t dtmb_cfg_14;
    volatile uint32_t dtmb_cfg_15;
    volatile uint32_t dtmb_cfg_16;
    volatile uint32_t dtmb_cfg_17;
    volatile uint32_t dtmb_cfg_18;
    volatile uint32_t dtmb_cfg_19;
    volatile uint32_t dtmb_cfg_1a;
    volatile uint32_t dtmb_cfg_1b;
    volatile uint32_t dtmb_cfg_1c;
    volatile uint32_t dtmb_cfg_1d;
    volatile uint32_t dtmb_cfg_1e;
    volatile uint32_t dtmb_cfg_1f;
    volatile uint32_t dtmb_cfg_20;
    volatile uint32_t dtmb_cfg_21;
    volatile uint32_t dtmb_cfg_22;
    volatile uint32_t dtmb_cfg_23;
    volatile uint32_t dtmb_cfg_24;
    volatile uint32_t dtmb_cfg_25;
    volatile uint32_t dtmb_cfg_26;
    volatile uint32_t dtmb_cfg_27;
    volatile uint32_t dtmb_cfg_28;
    volatile uint32_t dtmb_cfg_29;
    volatile uint32_t dtmb_cfg_2a;
    volatile uint32_t dtmb_cfg_2b;
    volatile uint32_t dtmb_cfg_2c;
    volatile uint32_t dtmb_cfg_2d;
    volatile uint32_t dtmb_cfg_2e;
    volatile uint32_t dtmb_cfg_2f;
    volatile uint32_t dtmb_cfg_30;
    volatile uint32_t dtmb_cfg_31;
    volatile uint32_t dtmb_cfg_32;
    volatile uint32_t dtmb_cfg_33;
    volatile uint32_t dtmb_cfg_34;
    volatile uint32_t dtmb_cfg_35;
    volatile uint32_t dtmb_cfg_36;
    volatile uint32_t dtmb_cfg_37;
    volatile uint32_t dtmb_cfg_38;
    volatile uint32_t dtmb_cfg_39;
    volatile uint32_t dtmb_cfg_3a;
    volatile uint32_t dtmb_cfg_3b;
    volatile uint32_t dtmb_cfg_3c;
    volatile uint32_t dtmb_cfg_3d;
    volatile uint32_t dtmb_cfg_3e;
    volatile uint32_t dtmb_cfg_3f;
    volatile uint32_t dtmb_cfg_40;
    volatile uint32_t dtmb_cfg_41;
    volatile uint32_t dtmb_cfg_42;
    volatile uint32_t dtmb_cfg_43;
    volatile uint32_t dtmb_cfg_44;
    volatile uint32_t dtmb_cfg_45;
    volatile uint32_t dtmb_cfg_46;
    volatile uint32_t dtmb_cfg_47;
    volatile uint32_t dtmb_cfg_48;
    volatile uint32_t dtmb_cfg_49;
    volatile uint32_t dtmb_cfg_4a;
    volatile uint32_t dtmb_cfg_4b;
    volatile uint32_t dtmb_cfg_4c;
    volatile uint32_t dtmb_cfg_4d;
    volatile uint32_t dtmb_cfg_4e;
    volatile uint32_t dtmb_cfg_4f;
    volatile uint32_t dtmb_cfg_50;
    volatile uint32_t dtmb_cfg_51;
    volatile uint32_t dtmb_cfg_52;
    volatile uint32_t dtmb_cfg_53;
    volatile uint32_t dtmb_cfg_54;
    volatile uint32_t dtmb_cfg_55;
    volatile uint32_t dtmb_cfg_56;
    volatile uint32_t dtmb_cfg_57;
    volatile uint32_t dtmb_cfg_58;
    volatile uint32_t dtmb_cfg_59;
    volatile uint32_t dtmb_cfg_5a;
    volatile uint32_t dtmb_cfg_5b;
    volatile uint32_t dtmb_cfg_5c;
    volatile uint32_t dtmb_cfg_5d;
    volatile uint32_t dtmb_cfg_5e;
    volatile uint32_t dtmb_cfg_5f;
    volatile uint32_t dtmb_cfg_60;
    volatile uint32_t dtmb_cfg_61;
    volatile uint32_t dtmb_cfg_62;
    volatile uint32_t dtmb_cfg_63;
    volatile uint32_t dtmb_cfg_64;
    //volatile uint32_t dtmb_cfg_65;
    //volatile uint32_t dtmb_cfg_66;
    //volatile uint32_t dtmb_cfg_67;
    //volatile uint32_t dtmb_cfg_68;
    //volatile uint32_t dtmb_cfg_69;
    //volatile uint32_t dtmb_cfg_6a;
    //volatile uint32_t dtmb_cfg_6b;
    //volatile uint32_t dtmb_cfg_6c;
    //volatile uint32_t dtmb_cfg_6d;
    //volatile uint32_t dtmb_cfg_6e;
    //volatile uint32_t dtmb_cfg_6f;
    //volatile uint32_t dtmb_cfg_70;
    //volatile uint32_t dtmb_cfg_71;
    //volatile uint32_t dtmb_cfg_72;
    //volatile uint32_t dtmb_cfg_73;
    //volatile uint32_t dtmb_cfg_74;
    //volatile uint32_t dtmb_cfg_75;
    //volatile uint32_t dtmb_cfg_76;
    //volatile uint32_t dtmb_cfg_77;
    //volatile uint32_t dtmb_cfg_78;
    //volatile uint32_t dtmb_cfg_79;
    //volatile uint32_t dtmb_cfg_7a;
    //volatile uint32_t dtmb_cfg_7b;
    //volatile uint32_t dtmb_cfg_7c;
    //volatile uint32_t dtmb_cfg_7d;
    //volatile uint32_t dtmb_cfg_7e;
    //volatile uint32_t dtmb_cfg_7f;
    //volatile uint32_t dtmb_cfg_80;
    //volatile uint32_t dtmb_cfg_81;
    //volatile uint32_t dtmb_cfg_82;
    //volatile uint32_t dtmb_cfg_83;
    //volatile uint32_t dtmb_cfg_84;
    //volatile uint32_t dtmb_cfg_85;
    //volatile uint32_t dtmb_cfg_86;
    //volatile uint32_t dtmb_cfg_87;
    //volatile uint32_t dtmb_cfg_88;
    //volatile uint32_t dtmb_cfg_89;
    //volatile uint32_t dtmb_cfg_8a;
    //volatile uint32_t dtmb_cfg_8b;
    //volatile uint32_t dtmb_cfg_8c;
    //volatile uint32_t dtmb_cfg_8d;
    //volatile uint32_t dtmb_cfg_8e;
    //volatile uint32_t dtmb_cfg_8f;
    //volatile uint32_t dtmb_cfg_90;
    //volatile uint32_t dtmb_cfg_91;
    //volatile uint32_t dtmb_cfg_92;
    //volatile uint32_t dtmb_cfg_93;
    //volatile uint32_t dtmb_cfg_94;
    //volatile uint32_t dtmb_cfg_95;
    //volatile uint32_t dtmb_cfg_96;
    //volatile uint32_t dtmb_cfg_97;
    //volatile uint32_t dtmb_cfg_98;
    //volatile uint32_t dtmb_cfg_99;
    //volatile uint32_t dtmb_cfg_9a;
    //volatile uint32_t dtmb_cfg_9b;
    //volatile uint32_t dtmb_cfg_9c;
    //volatile uint32_t dtmb_cfg_9d;
    //volatile uint32_t dtmb_cfg_9e;
    //volatile uint32_t dtmb_cfg_9f;
} dtmb_cfg_regs_t;

typedef union dtmb_cfg_00 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved0:32;
    } b;
} dtmb_cfg_00_t;

typedef union dtmb_cfg_01 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned ctrl_sw_rst:1;
        unsigned reserved0:31;
    } b;
} dtmb_cfg_01_t;

typedef union dtmb_cfg_02 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned testbus_addr:16;
        unsigned testbus_en:1;
        unsigned reserved0:15;
    } b;
} dtmb_cfg_02_t;

typedef union dtmb_cfg_03 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned tb_act_width:5;
        unsigned reserved0:3;
        unsigned tb_dc_mk:3;
        unsigned reserved1:1;
        unsigned tb_capture_stop:1;
        unsigned tb_self_test:1;
        unsigned reserved2:18;
    } b;
} dtmb_cfg_03_t;

typedef union dtmb_cfg_04 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned tb_v:32;
    } b;
} dtmb_cfg_04_t;

typedef union dtmb_cfg_05 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned tb_addr_begin:32;
    } b;
} dtmb_cfg_05_t;

typedef union dtmb_cfg_06 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned tb_addr_end:32;
    } b;
} dtmb_cfg_06_t;

typedef union dtmb_cfg_07 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned ctrl_fsm_enable:1;
        unsigned ctrl_afifo_enable:1;
        unsigned ctrl_agc_enable:1;
        unsigned ctrl_ddc_enable:1;
        unsigned ctrl_dc_enable:1;
        unsigned ctrl_acf_enable:1;
        unsigned ctrl_src_enable:1;
        unsigned ctrl_dagc_enable:1;
        unsigned ctrl_sfifo_enable:1;
        unsigned ctrl_iqib_enable:1;
        unsigned ctrl_cci_enable:1;
        unsigned ctrl_fft2048_enable:1;
        unsigned ctrl_ts_enable:1;
        unsigned ctrl_corr_enable:1;
        unsigned ctrl_fe_enable:1;
        unsigned ctrl_fft512_enable:1;
        unsigned ctrl_pnphase_enable:1;
        unsigned ctrl_sfo_enable:1;
        unsigned ctrl_pm_enable:1;
        unsigned ctrl_che_enable:1;
        unsigned ctrl_fec_enable:1;
        unsigned ctrl_tps_enable:1;
        unsigned reserved0:10;
    } b;
} dtmb_cfg_07_t;

typedef union dtmb_cfg_08 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned ctrl_src_pnphase_loop:1;
        unsigned ctrl_src_sfo_loop:1;
        unsigned ctrl_ddc_fcfo_loop:1;
        unsigned ctrl_ddc_icfo_loop:1;
        unsigned reserved0:28;
    } b;
} dtmb_cfg_08_t;

typedef union dtmb_cfg_09 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned ctrl_fsm_state:5;
        unsigned reserved0:3;
        unsigned ctrl_fsm_v:1;
        unsigned reserved1:23;
    } b;
} dtmb_cfg_09_t;

typedef union dtmb_cfg_0a {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned ctrl_fast_agc:1;
        unsigned ctrl_agc_bypass:1;
        unsigned ctrl_pm_hold:1;
        unsigned reserved0:29;
    } b;
} dtmb_cfg_0a_t;

typedef union dtmb_cfg_0b {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned ctrl_ts_q:10;
        unsigned reserved0:2;
        unsigned ctrl_pnphase_q:7;
        unsigned reserved1:1;
        unsigned ctrl_sfo_q:4;
        unsigned ctrl_cfo_q:8;
    } b;
} dtmb_cfg_0b_t;

typedef union dtmb_cfg_0c {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned ctrl_agc_to_th:8;
        unsigned ctrl_ts_to_th:4;
        unsigned ctrl_pnphase_to_th:4;
        unsigned ctrl_sfo_to_th:4;
        unsigned ctrl_fe_to_th:4;
        unsigned reserved0:8;
    } b;
} dtmb_cfg_0c_t;

typedef union dtmb_cfg_0d {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved0:32;
    } b;
} dtmb_cfg_0d_t;

typedef union dtmb_cfg_0e {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved0:32;
    } b;
} dtmb_cfg_0e_t;

typedef union dtmb_cfg_0f {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved0:32;
    } b;
} dtmb_cfg_0f_t;

typedef union dtmb_cfg_10 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned afifo_nco_rate:8;
        unsigned afifo_data_format:1;
        unsigned afifo_bypass:1;
        unsigned reserved0:22;
    } b;
} dtmb_cfg_10_t;

typedef union dtmb_cfg_11 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned agc_target:4;
        unsigned agc_cal_intv:2;
        unsigned reserved0:2;
        unsigned agc_gain_step2:6;
        unsigned reserved1:2;
        unsigned agc_gain_step1:6;
        unsigned reserved2:2;
        unsigned agc_a_filter_coef2:3;
        unsigned reserved3:1;
        unsigned agc_a_filter_coef1:3;
        unsigned reserved4:1;
    } b;
} dtmb_cfg_11_t;

typedef union dtmb_cfg_12 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned agc_imp_thresh:4;
        unsigned agc_imp_en:1;
        unsigned agc_iq_exchange:1;
        unsigned reserved0:2;
        unsigned agc_clip_ratio:5;
        unsigned reserved1:3;
        unsigned agc_signal_clip_thr:6;
        unsigned reserved2:2;
        unsigned agc_sd_rate:7;
        unsigned reserved3:1;
    } b;
} dtmb_cfg_12_t;

typedef union dtmb_cfg_13 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned agc_rffb_value:11;
        unsigned reserved0:1;
        unsigned agc_iffb_value:11;
        unsigned reserved1:1;
        unsigned agc_gain_step_rf:1;
        unsigned agc_rfgain_freeze:1;
        unsigned agc_tuning_slope:1;
        unsigned agc_rffb_set:1;
        unsigned agc_gain_step_if:1;
        unsigned agc_ifgain_freeze:1;
        unsigned agc_if_only:1;
        unsigned agc_iffb_set:1;
    } b;
} dtmb_cfg_13_t;

typedef union dtmb_cfg_14 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned agc_rffb_gain_sat_i:8;
        unsigned agc_rffb_gain_sat:8;
        unsigned agc_iffb_gain_sat_i:8;
        unsigned agc_iffb_gain_sat:8;
    } b;
} dtmb_cfg_14_t;

typedef union dtmb_cfg_15 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned ddc_phase:15;
        unsigned reserved0:17;
    } b;
} dtmb_cfg_15_t;

typedef union dtmb_cfg_16 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned ddc_delta_phase:25;
        unsigned reserved0:3;
        unsigned ddc_feedback_clear:1;
        unsigned reserved1:3;
    } b;
} dtmb_cfg_16_t;

typedef union dtmb_cfg_17 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned ddc_bypass:1;
        unsigned reserved0:31;
    } b;
} dtmb_cfg_17_t;

typedef union dtmb_cfg_18 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned dc_hold:1;
        unsigned dc_set_val:1;
        unsigned dc_alpha:2;
        unsigned reserved0:28;
    } b;
} dtmb_cfg_18_t;

typedef union dtmb_cfg_19 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned dc_set_avg_q:16;
        unsigned dc_set_avg_i:16;
    } b;
} dtmb_cfg_19_t;

typedef union dtmb_cfg_1a {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef65:11;
        unsigned reserved0:1;
        unsigned coef66:11;
        unsigned reserved1:1;
        unsigned acf_bypass:1;
        unsigned reserved2:7;
    } b;
} dtmb_cfg_1a_t;

typedef union dtmb_cfg_1b {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef63:11;
        unsigned reserved0:1;
        unsigned coef64:11;
        unsigned reserved1:9;
    } b;
} dtmb_cfg_1b_t;

typedef union dtmb_cfg_1c {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef62:10;
        unsigned reserved0:22;
    } b;
} dtmb_cfg_1c_t;

typedef union dtmb_cfg_1d {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef60:10;
        unsigned reserved0:2;
        unsigned coef61:10;
        unsigned reserved1:10;
    } b;
} dtmb_cfg_1d_t;

typedef union dtmb_cfg_1e {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef59:9;
        unsigned reserved0:23;
    } b;
} dtmb_cfg_1e_t;

typedef union dtmb_cfg_1f {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef57:9;
        unsigned reserved0:3;
        unsigned coef58:9;
        unsigned reserved1:11;
    } b;
} dtmb_cfg_1f_t;

typedef union dtmb_cfg_20 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef54:8;
        unsigned coef55:8;
        unsigned coef56:8;
        unsigned reserved0:8;
    } b;
} dtmb_cfg_20_t;

typedef union dtmb_cfg_21 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef53:7;
        unsigned reserved0:25;
    } b;
} dtmb_cfg_21_t;

typedef union dtmb_cfg_22 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef49:7;
        unsigned reserved0:1;
        unsigned coef50:7;
        unsigned reserved1:1;
        unsigned coef51:7;
        unsigned reserved2:1;
        unsigned coef52:7;
        unsigned reserved3:1;
    } b;
} dtmb_cfg_22_t;

typedef union dtmb_cfg_23 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef45:7;
        unsigned reserved0:1;
        unsigned coef46:7;
        unsigned reserved1:1;
        unsigned coef47:7;
        unsigned reserved2:1;
        unsigned coef48:7;
        unsigned reserved3:1;
    } b;
} dtmb_cfg_23_t;

typedef union dtmb_cfg_24 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef42:6;
        unsigned reserved0:2;
        unsigned coef43:6;
        unsigned reserved1:2;
        unsigned coef44:6;
        unsigned reserved2:10;
    } b;
} dtmb_cfg_24_t;

typedef union dtmb_cfg_25 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef38:6;
        unsigned reserved0:2;
        unsigned coef39:6;
        unsigned reserved1:2;
        unsigned coef40:6;
        unsigned reserved2:2;
        unsigned coef41:6;
        unsigned reserved3:2;
    } b;
} dtmb_cfg_25_t;

typedef union dtmb_cfg_26 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef34:6;
        unsigned reserved0:2;
        unsigned coef35:6;
        unsigned reserved1:2;
        unsigned coef36:6;
        unsigned reserved2:2;
        unsigned coef37:6;
        unsigned reserved3:2;
    } b;
} dtmb_cfg_26_t;

typedef union dtmb_cfg_27 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef30:6;
        unsigned reserved0:2;
        unsigned coef31:6;
        unsigned reserved1:2;
        unsigned coef32:6;
        unsigned reserved2:2;
        unsigned coef33:6;
        unsigned reserved3:2;
    } b;
} dtmb_cfg_27_t;

typedef union dtmb_cfg_28 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef27:5;
        unsigned reserved0:3;
        unsigned coef28:5;
        unsigned reserved1:3;
        unsigned coef29:5;
        unsigned reserved2:11;
    } b;
} dtmb_cfg_28_t;

typedef union dtmb_cfg_29 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef23:5;
        unsigned reserved0:3;
        unsigned coef24:5;
        unsigned reserved1:3;
        unsigned coef25:5;
        unsigned reserved2:3;
        unsigned coef26:5;
        unsigned reserved3:3;
    } b;
} dtmb_cfg_29_t;

typedef union dtmb_cfg_2a {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef19:5;
        unsigned reserved0:3;
        unsigned coef20:5;
        unsigned reserved1:3;
        unsigned coef21:5;
        unsigned reserved2:3;
        unsigned coef22:5;
        unsigned reserved3:3;
    } b;
} dtmb_cfg_2a_t;

typedef union dtmb_cfg_2b {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef15:5;
        unsigned reserved0:3;
        unsigned coef16:5;
        unsigned reserved1:3;
        unsigned coef17:5;
        unsigned reserved2:3;
        unsigned coef18:5;
        unsigned reserved3:3;
    } b;
} dtmb_cfg_2b_t;

typedef union dtmb_cfg_2c {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef08:4;
        unsigned coef09:4;
        unsigned coef10:4;
        unsigned coef11:4;
        unsigned coef12:4;
        unsigned coef13:4;
        unsigned coef14:4;
        unsigned reserved0:4;
    } b;
} dtmb_cfg_2c_t;

typedef union dtmb_cfg_2d {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned coef00:4;
        unsigned coef01:4;
        unsigned coef02:4;
        unsigned coef03:4;
        unsigned coef04:4;
        unsigned coef05:4;
        unsigned coef06:4;
        unsigned coef07:4;
    } b;
} dtmb_cfg_2d_t;

typedef union dtmb_cfg_2e {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned src_norm_inrate:23;
        unsigned reserved0:9;
    } b;
} dtmb_cfg_2e_t;

typedef union dtmb_cfg_2f {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned src_init_phase:24;
        unsigned src_init_ini:1;
        unsigned reserved0:7;
    } b;
} dtmb_cfg_2f_t;

typedef union dtmb_cfg_30 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned sfifo_out_len:4;
        unsigned reserved0:28;
    } b;
} dtmb_cfg_30_t;

typedef union dtmb_cfg_31 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned dagc_target_pow_n:6;
        unsigned reserved0:2;
        unsigned dagc_target_pow_p:6;
        unsigned reserved1:2;
        unsigned dagc_gain_ctrl:8;
        unsigned dagc_bw:3;
        unsigned reserved2:1;
        unsigned dagc_hold:1;
        unsigned reserved3:3;
    } b;
} dtmb_cfg_31_t;

typedef union dtmb_cfg_32 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned iqib_step_b:2;
        unsigned iqib_step_a:2;
        unsigned iqib_period:3;
        unsigned reserved0:25;
    } b;
} dtmb_cfg_32_t;

typedef union dtmb_cfg_33 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned iqib_set_b:12;
        unsigned iqib_set_a:10;
        unsigned reserved0:2;
        unsigned iqib_set_val:1;
        unsigned iqib_hold:1;
        unsigned reserved1:6;
    } b;
} dtmb_cfg_33_t;

typedef union dtmb_cfg_34 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned cci_rpsq_n:10;
        unsigned reserved0:2;
        unsigned cci_rp_n:13;
        unsigned reserved1:3;
        unsigned cci_det_en:1;
        unsigned cci_bypass:1;
        unsigned reserved2:2;
    } b;
} dtmb_cfg_34_t;

typedef union dtmb_cfg_35 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned cci_avr_times:3;
        unsigned reserved0:1;
        unsigned cci_det_thres:3;
        unsigned reserved1:25;
    } b;
} dtmb_cfg_35_t;

typedef union dtmb_cfg_36 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned cci_notch1_a1:10;
        unsigned reserved0:2;
        unsigned cci_notch1_en:1;
        unsigned reserved1:19;
    } b;
} dtmb_cfg_36_t;

typedef union dtmb_cfg_37 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned cci_notch1_b1:10;
        unsigned reserved0:2;
        unsigned cci_notch1_a2:10;
        unsigned reserved1:10;
    } b;
} dtmb_cfg_37_t;

typedef union dtmb_cfg_38 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned cci_notch2_a1:10;
        unsigned reserved0:2;
        unsigned cci_notch2_en:1;
        unsigned reserved1:3;
        unsigned cci_mpthres:16;
    } b;
} dtmb_cfg_38_t;

typedef union dtmb_cfg_39 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned cci_notch2_b1:10;
        unsigned reserved0:2;
        unsigned cci_notch2_a2:10;
        unsigned reserved1:10;
    } b;
} dtmb_cfg_39_t;

typedef union dtmb_cfg_3a {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned ts_gain:2;
        unsigned reserved0:2;
        unsigned ts_sat_shift:3;
        unsigned reserved1:1;
        unsigned ts_fixpn_en:1;
        unsigned ts_fixpn:2;
        unsigned reserved2:21;
    } b;
} dtmb_cfg_3a_t;

typedef union dtmb_cfg_3b {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned fe_lock_len:4;
        unsigned fe_sat_shift:3;
        unsigned reserved0:1;
        unsigned fe_cut:4;
        unsigned reserved1:4;
        unsigned fe_modify:16;
    } b;
} dtmb_cfg_3b_t;

typedef union dtmb_cfg_3c {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned pnphase_offset2:4;
        unsigned pnphase_offset1:4;
        unsigned pnphase_offset0:4;
        unsigned reserved0:20;
    } b;
} dtmb_cfg_3c_t;

typedef union dtmb_cfg_3d {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned pnphase_gain:2;
        unsigned reserved0:2;
        unsigned pnphase_sat_shift:4;
        unsigned pnphase_cut:4;
        unsigned reserved1:4;
        unsigned pnphase_modify:16;
    } b;
} dtmb_cfg_3d_t;

typedef union dtmb_cfg_3e {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned sfo_cfo_pn0_modify:16;
        unsigned sfo_sfo_pn0_modify:16;
    } b;
} dtmb_cfg_3e_t;

typedef union dtmb_cfg_3f {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned sfo_cfo_pn1_modify:16;
        unsigned sfo_sfo_pn1_modify:16;
    } b;
} dtmb_cfg_3f_t;

typedef union dtmb_cfg_40 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned sfo_cfo_pn2_modify:16;
        unsigned sfo_sfo_pn2_modify:16;
    } b;
} dtmb_cfg_40_t;

typedef union dtmb_cfg_41 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned sfo_sat_shift:4;
        unsigned sfo_gain:2;
        unsigned sfo_timingoff_en:1;
        unsigned sfo_timing_offset:1;
        unsigned sfo_dist:2;
        unsigned reserved0:2;
        unsigned sfo_cfo_cut:4;
        unsigned sfo_sfo_cut:4;
        unsigned reserved1:12;
    } b;
} dtmb_cfg_41_t;

typedef union dtmb_cfg_42 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned pm_gd_short_pst:5;
        unsigned reserved0:3;
        unsigned pm_gd_short_pre:5;
        unsigned reserved1:3;
        unsigned pm_gd_long_pst:6;
        unsigned reserved2:2;
        unsigned pm_gd_long_pre:6;
        unsigned reserved3:2;
    } b;
} dtmb_cfg_42_t;

typedef union dtmb_cfg_43 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned pm_big_offset:5;
        unsigned reserved0:3;
        unsigned pm_small_offset:5;
        unsigned reserved1:3;
        unsigned pm_big_shift:4;
        unsigned pm_small_shift:4;
        unsigned pm_noise_gain:3;
        unsigned pm_select_gain:3;
        unsigned pm_select_ch_gain:2;
    } b;
} dtmb_cfg_43_t;

typedef union dtmb_cfg_44 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned pm_accu_times:4;
        unsigned reserved0:28;
    } b;
} dtmb_cfg_44_t;

typedef union dtmb_cfg_45 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned tps_run_tim_limit:10;
        unsigned reserved0:2;
        unsigned tps_suc_limit:7;
        unsigned reserved1:1;
        unsigned tps_q_th:7;
        unsigned reserved2:1;
        unsigned tps_alpha:3;
        unsigned reserved3:1;
    } b;
} dtmb_cfg_45_t;

typedef union dtmb_cfg_46 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned tps_known:1;
        unsigned static_channel:1;
        unsigned reserved0:2;
        unsigned constell:2;
        unsigned reserved1:2;
        unsigned code_rate:2;
        unsigned intlv_mode:1;
        unsigned qam4_nr:1;
        unsigned freq_reverse:1;
        unsigned reserved2:19;
    } b;
} dtmb_cfg_46_t;

typedef union dtmb_cfg_47 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned ldpc_it_min:8;
        unsigned ldpc_it_max:8;
        unsigned ldpc_it_auto:1;
        unsigned ldpc_it_dchk:1;
        unsigned bch_off:1;
        unsigned ts_clk_neg:1;
        unsigned ts_fast:1;
        unsigned dc_ugt:1;
        unsigned sw_reset_freq_di:1;
        unsigned sw_reset_4qam_nr:1;
        unsigned sw_reset_time_di:1;
        unsigned sw_reset_ldpc:1;
        unsigned sw_reset_bch:1;
        unsigned sw_reset_ber:1;
        unsigned tbus_cfg:3;
        unsigned fifo_base:1;
    } b;
} dtmb_cfg_47_t;

typedef union dtmb_cfg_48 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned fec_debug_len:16;
        unsigned fec_debug_mode:1;
        unsigned fec_debug_on:1;
        unsigned fec_lock_cfg:3;
        unsigned fec_lost_cfg:3;
        unsigned bad_to_zero:1;
        unsigned fec_debug_spare:7;
    } b;
} dtmb_cfg_48_t;

typedef union dtmb_cfg_49 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned data_ddr_adr:32;
    } b;
} dtmb_cfg_49_t;

typedef union dtmb_cfg_4a {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned debug_ddr_adr:32;
    } b;
} dtmb_cfg_4a_t;

typedef union dtmb_cfg_4b {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned sim_dat_b0:6;
        unsigned sim_dat_b1:6;
        unsigned sim_dat_b2:6;
        unsigned sim_dat_b3:6;
        unsigned sim_dat_b4:6;
        unsigned reserved:2;
    } b;
} dtmb_cfg_4b_t;

typedef union dtmb_cfg_4c {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned sim_dat_b5:6;
        unsigned sim_end:3;
        unsigned sim_vld:1;
        unsigned sim_head:1;
        unsigned sim_ini:1;
        unsigned sim_mode:1;
        unsigned reserved:19;
    } b;
} dtmb_cfg_4c_t;

typedef union dtmb_cfg_4d {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned che_eqin_f_mcci_thr:4;
        unsigned che_ch_mh_thr      :12;
        unsigned che_tune_thr       :4;
        unsigned che_tune_cnt_thr   :12;
    } b;
} dtmb_cfg_4d_t;

typedef union dtmb_cfg_4e {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned che_ch_noise_pow   :15;
        unsigned che_ch_noise_pow_en:1;
        unsigned che_up_noise_pow   :15;
        unsigned che_up_noise_pow_en:1;
    } b;
} dtmb_cfg_4e_t;

typedef union dtmb_cfg_4f {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned che_up_mh_thr:13;
        unsigned reserved0    :3;
        unsigned che_belta    :5;
        unsigned che_belta_en :1;
        unsigned reserved1    :2;
        unsigned che_alpha    :5;
        unsigned che_alpha_en :1;
        unsigned reserved2    :2;
    } b;
} dtmb_cfg_4f_t;

typedef union dtmb_cfg_50 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned che_clk_fcy_indx    :2;
        unsigned che_iter_time_en    :1;
        unsigned reserved0           :1;
        unsigned che_iter_time_mobile:4;
        unsigned che_iter_time_static:4;
        unsigned bp_last_itera       :1;
        unsigned reserved1           :19;
    } b;
} dtmb_cfg_50_t;

typedef union dtmb_cfg_51 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned che_m_init_snr1:8;
        unsigned che_m_init_snr2:8;
        unsigned che_m_init_snr3:8;
        unsigned che_m_init_snr4:8;
    } b;
} dtmb_cfg_51_t;

typedef union dtmb_cfg_52 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned frame_mc_header_dly:16;
        unsigned pn_mc_header_dly   :16;
    } b;
} dtmb_cfg_52_t;

typedef union dtmb_cfg_53 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned frame_sc_header_dly:16;
        unsigned pn_sc_header_dly   :16;
    } b;
} dtmb_cfg_53_t;

typedef union dtmb_cfg_54 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned seg_bypass       :1;
        unsigned seg_num_1seg_log2:3;
        unsigned seg_alpha        :3;
        unsigned seg_read_val     :1;
        unsigned seg_read_addr    :12;
        unsigned reserved         :12;
    } b;
} dtmb_cfg_54_t;

typedef union dtmb_cfg_55 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved         :32;
    } b;
} dtmb_cfg_55_t;

typedef union dtmb_cfg_56 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved         :32;
    } b;
} dtmb_cfg_56_t;

typedef union dtmb_cfg_57 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved         :32;
    } b;
} dtmb_cfg_57_t;

typedef union dtmb_cfg_58 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved         :32;
    } b;
} dtmb_cfg_58_t;

typedef union dtmb_cfg_59 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved         :32;
    } b;
} dtmb_cfg_59_t;

typedef union dtmb_cfg_5a {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved         :32;
    } b;
} dtmb_cfg_5a_t;

typedef union dtmb_cfg_5b {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved         :32;
    } b;
} dtmb_cfg_5b_t;

typedef union dtmb_cfg_5c {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved         :32;
    } b;
} dtmb_cfg_5c_t;

typedef union dtmb_cfg_5d {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved         :32;
    } b;
} dtmb_cfg_5d_t;

typedef union dtmb_cfg_5e {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved         :32;
    } b;
} dtmb_cfg_5e_t;

typedef union dtmb_cfg_5f {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved         :32;
    } b;
} dtmb_cfg_5f_t;

typedef union dtmb_cfg_60 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved         :32;
    } b;
} dtmb_cfg_60_t;

typedef union dtmb_cfg_61 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved         :32;
    } b;
} dtmb_cfg_61_t;

typedef union dtmb_cfg_62 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved         :32;
    } b;
} dtmb_cfg_62_t;

typedef union dtmb_cfg_63 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved         :32;
    } b;
} dtmb_cfg_63_t;

typedef union dtmb_cfg_64 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved         :32;
    } b;
} dtmb_cfg_64_t;

typedef struct dvbc_cfg_regs {
    volatile uint32_t dvbc_cfg_00;
    volatile uint32_t dvbc_cfg_04;
    volatile uint32_t dvbc_cfg_08;
    volatile uint32_t dvbc_cfg_0c;
    volatile uint32_t dvbc_cfg_10;
    volatile uint32_t dvbc_cfg_14;
    volatile uint32_t dvbc_cfg_18;
    volatile uint32_t dvbc_cfg_1c;
    volatile uint32_t dvbc_cfg_20;
    volatile uint32_t dvbc_cfg_24;
    volatile uint32_t dvbc_cfg_28;
    volatile uint32_t dvbc_cfg_2c;
    volatile uint32_t dvbc_cfg_30;
    volatile uint32_t dvbc_cfg_34;
    volatile uint32_t dvbc_cfg_38;
    volatile uint32_t dvbc_cfg_3c;
    volatile uint32_t dvbc_cfg_40;
    volatile uint32_t dvbc_cfg_44;
    volatile uint32_t dvbc_cfg_48;
    volatile uint32_t dvbc_cfg_4c;
    volatile uint32_t dvbc_cfg_50;
    volatile uint32_t dvbc_cfg_54;
    volatile uint32_t dvbc_cfg_58;
    volatile uint32_t dvbc_cfg_5c;
    volatile uint32_t dvbc_cfg_60;
    volatile uint32_t dvbc_cfg_64;
    volatile uint32_t dvbc_cfg_68;
    volatile uint32_t dvbc_cfg_6c;
    volatile uint32_t dvbc_cfg_70;
    volatile uint32_t dvbc_cfg_74;
    volatile uint32_t dvbc_cfg_78;
    volatile uint32_t dvbc_cfg_7c;
    volatile uint32_t dvbc_cfg_80;
    volatile uint32_t dvbc_cfg_84;
    volatile uint32_t dvbc_cfg_88;
    volatile uint32_t dvbc_cfg_8c;
    volatile uint32_t dvbc_cfg_90;
    volatile uint32_t dvbc_cfg_94;
    volatile uint32_t dvbc_cfg_98;
    volatile uint32_t dvbc_cfg_9c;
    volatile uint32_t dvbc_cfg_a0;
    volatile uint32_t dvbc_cfg_a4;
    volatile uint32_t dvbc_cfg_a8;
    volatile uint32_t dvbc_cfg_ac;
    volatile uint32_t dvbc_cfg_b0;
    volatile uint32_t dvbc_cfg_b4;
    volatile uint32_t dvbc_cfg_b8;
    volatile uint32_t dvbc_cfg_bc;
    volatile uint32_t dvbc_cfg_c0;
    volatile uint32_t dvbc_cfg_c4;
    volatile uint32_t dvbc_cfg_c8;
    volatile uint32_t dvbc_cfg_cc;
    volatile uint32_t dvbc_cfg_d0;
    volatile uint32_t dvbc_cfg_d4;
    volatile uint32_t dvbc_cfg_d8;
    volatile uint32_t dvbc_cfg_dc;
    volatile uint32_t dvbc_cfg_e0;
    volatile uint32_t dvbc_cfg_e4;
    volatile uint32_t dvbc_cfg_e8;
    volatile uint32_t dvbc_cfg_ec;
} dvbc_cfg_regs_t;

typedef union dvbc_cfg_00 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved:32;
    } b;
} dvbc_cfg_00_t;

typedef union dvbc_cfg_04 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned sw_qam_enable:1;
        unsigned qam_imq_cfg:1;
        unsigned reserved0:1;
        unsigned nyq_bypass_cfg:1;
        unsigned fsm_en:1;
        unsigned fast_agc:1;
        unsigned reserved1:2;
        unsigned dc_enable:1;
        unsigned dc_alpha:3;
        unsigned not_used:20;
    } b;
} dvbc_cfg_04_t;

typedef union dvbc_cfg_08 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned qam_mode_cfg:3;
        unsigned qam_test_en:1;
        unsigned qam_test_addr:5;
        unsigned reserved:7;
        unsigned hcap_en:1;
        unsigned dvbc_topstate_ct1:3;
        unsigned fsm_state_d:3;
        unsigned fsm_state_v:1;
        unsigned not_used:8;
    } b;
} dvbc_cfg_08_t;

typedef union dvbc_cfg_0c {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned symb_cnt_cfg:16;
        unsigned adc_cnt_cfg:16;
    } b;
} dvbc_cfg_0c_t;

typedef union dvbc_cfg_10 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned rs_cnt_cfg:16;
        unsigned afifo_nco_rate:8;
        unsigned afifo_bypass:1;
        unsigned not_used:7;
    } b;
} dvbc_cfg_10_t;

typedef union dvbc_cfg_14 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_snr:12;
        unsigned ber_before_rs:20;
    } b;
} dvbc_cfg_14_t;

typedef union dvbc_cfg_18 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned tst_sync:7;
        unsigned not_used:9;
        unsigned per_rs:16;
    } b;
} dvbc_cfg_18_t;

typedef union dvbc_cfg_1c {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_dov:1;
        unsigned eq_doq:12;
        unsigned eq_doi:12;
        unsigned not_used:7;
    } b;
} dvbc_cfg_1c_t;

typedef union dvbc_cfg_20 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned phs_reset_cfg:1;
        unsigned tim_mu2_cfg_accurate:5;
        unsigned tim_mu1_cfg_accurate:5;
        unsigned tim_sync_cfg_accurate:4;
        unsigned tim_trk_cfg_accurate:4;
        unsigned tim_shr_cfg_accurate:4;
        unsigned sw_tim_select:1;
        unsigned phs_mu:4;
        unsigned phs_track_enable:1;
        unsigned phs_track_eqin_enable:1;
        unsigned phs_track_in_smma:1;
        unsigned not_used:1;
    } b;
} dvbc_cfg_20_t;

typedef union dvbc_cfg_24 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned phs_offset_cfg:23;
        unsigned tim_mu1_min:5;
        unsigned tim_mu2_min:4;
    } b;
} dvbc_cfg_24_t;

typedef union dvbc_cfg_28 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned phs_offset_act:23;
        unsigned not_used:9;
    } b;
} dvbc_cfg_28_t;

typedef union dvbc_cfg_2c {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_cfg_max_frq_off:30;
        unsigned not_used:2;
    } b;
} dvbc_cfg_2c_t;

typedef union dvbc_cfg_30 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned sw_tim_sweep_onoff_cfg:1;
        unsigned tim_sweep_speed_cfg:5;
        unsigned tim_mu2_cfg_coarse:5;
        unsigned tim_mu1_cfg_coarse:5;
        unsigned tim_sync_cfg_coarse:4;
        unsigned tim_trk_cfg_coarse:4;
        unsigned tim_shr_cfg_coarse:4;
        unsigned tim_reset_cfg:1;
        unsigned hw_fsm_ctrl:3;
    } b;
} dvbc_cfg_30_t;

typedef union dvbc_cfg_34 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned sw_symbol_rate:16;
        unsigned sampling_rate:16;
    } b;
} dvbc_cfg_34_t;

typedef union dvbc_cfg_38 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned tim_sweep_range_cfg:24;
        unsigned not_used:8;
    } b;
} dvbc_cfg_38_t;

typedef union dvbc_cfg_3c {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned q_uneven_report:32;
    } b;
} dvbc_cfg_3c_t;

typedef union dvbc_cfg_40 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned sync_trk_cfg:4;
        unsigned sync_acq_cfg:4;
        unsigned sync_reset_cfg:1;
        unsigned hw_symbol_rate_step:7;
        unsigned not_used:16;
    } b;
} dvbc_cfg_40_t;

typedef union dvbc_cfg_44 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned ts_ctrl_cfg:4;
        unsigned ts_serial_cfg:1;
        unsigned reserved:3;
        unsigned hw_symbol_rate_max:16;
        unsigned not_used:8;
    } b;
} dvbc_cfg_44_t;

typedef union dvbc_cfg_48 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned ted_sel_cfg:3;
        unsigned reserved:5;
        unsigned hw_symbol_rate_min:16;
        unsigned not_used:8;
    } b;
} dvbc_cfg_48_t;

typedef union dvbc_cfg_4c {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_miu_test:8;
        unsigned eq_q_uneven_cfg:8;
        unsigned eq_extra_tag_conf:2;
        unsigned not_used:14;
    } b;
} dvbc_cfg_4c_t;

typedef union dvbc_cfg_50 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_cfg_seglen:3;
        unsigned sw_eq_enable:1;
        unsigned ted_disable:1;
        unsigned hw_eq_dfe_disable:1;
        unsigned reserved0:2;
        unsigned eq_cfg_cr_shift_time:4;
        unsigned input_state:12;
        unsigned input_state_en:1;
        unsigned reserved1:3;
        unsigned sw_eq_smma_reset_ctrl:1;
        unsigned reserved2:1;
        unsigned not_used:2;
    } b;
} dvbc_cfg_50_t;

typedef union dvbc_cfg_54 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_cfg_dfemiu2:4;
        unsigned eq_cfg_dfemiu1:4;
        unsigned eq_cfg_dfemiu0:4;
        unsigned reserved:4;
        unsigned eq_cfg_ffemiu2:4;
        unsigned eq_cfg_ffemiu1:4;
        unsigned eq_cfg_ffemiu0:4;
        unsigned eq_cfg_firbeta1:2;
        unsigned eq_cfg_firbeta0:2;
    } b;
} dvbc_cfg_54_t;

typedef union dvbc_cfg_58 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_cfg_initpos1:6;
        unsigned reserved0:2;
        unsigned eq_cfg_initpos0:6;
        unsigned reserved1:2;
        unsigned eq_cfg_phstr_lp1:8;
        unsigned eq_cfg_phstr_lp0:8;
    } b;
} dvbc_cfg_58_t;

typedef union dvbc_cfg_5c {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_cfg_initvalq0:10;
        unsigned reserved:6;
        unsigned eq_cfg_initvali0:10;
        unsigned not_used:6;
    } b;
} dvbc_cfg_5c_t;

typedef union dvbc_cfg_60 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_cfg_initvalq1:10;
        unsigned reserved:6;
        unsigned eq_cfg_initvali1:10;
        unsigned not_used:6;
    } b;
} dvbc_cfg_60_t;

typedef union dvbc_cfg_64 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_cfg_crth3tim:6;
        unsigned reserved0:2;
        unsigned eq_cfg_crth2tim:6;
        unsigned reserved1:2;
        unsigned eq_cfg_crth1:6;
        unsigned reserved2:2;
        unsigned eq_cfg_crth0:6;
        unsigned not_used:2;
    } b;
} dvbc_cfg_64_t;

typedef union dvbc_cfg_68 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_cfg_crth3snr:12;
        unsigned reserved:4;
        unsigned eq_cfg_crth2snr:12;
        unsigned not_used:4;
    } b;
} dvbc_cfg_68_t;

typedef union dvbc_cfg_6c {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_cfg_crppmth2:6;
        unsigned reserved0:2;
        unsigned eq_cfg_crppmth1:7;
        unsigned reserved1:1;
        unsigned eq_cfg_crppmth0:5;
        unsigned eq_cr_amp_th:11;
    } b;
} dvbc_cfg_6c_t;

typedef union dvbc_cfg_70 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_cfg_crlpk2:2;
        unsigned reserved:6;
        unsigned eq_cfg_crlpk1:8;
        unsigned eq_cfg_crlpk0_s:8;
        unsigned eq_cfg_crlpk0:8;
    } b;
} dvbc_cfg_70_t;

typedef union dvbc_cfg_74 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_cfg_mma2lms:12;
        unsigned reserved:4;
        unsigned eq_cfg_ddlms:12;
        unsigned not_used:4;
    } b;
} dvbc_cfg_74_t;

typedef union dvbc_cfg_78 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned adc_in:10;
        unsigned reserved:22;
    } b;
} dvbc_cfg_78_t;

typedef union dvbc_cfg_7c {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_cfg_mma:12;
        unsigned eq_cfg_norm:12;
        unsigned not_used:8;
    } b;
} dvbc_cfg_7c_t;

typedef union dvbc_cfg_80 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_cfg_smma1:12;
        unsigned eq_cfg_smma0:12;
        unsigned not_used:8;
    } b;
} dvbc_cfg_80_t;

typedef union dvbc_cfg_84 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_cfg_smma3:12;
        unsigned eq_cfg_smma2:12;
        unsigned not_used:8;
    } b;
} dvbc_cfg_84_t;

typedef union dvbc_cfg_88 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_cfg_smma5:12;
        unsigned eq_cfg_smma4:12;
        unsigned not_used:8;
    } b;
} dvbc_cfg_88_t;

typedef union dvbc_cfg_8c {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_cfg_smma7:12;
        unsigned eq_cfg_smma6:12;
        unsigned not_used:8;
    } b;
} dvbc_cfg_8c_t;

typedef union dvbc_cfg_90 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_offset_cfg:11;
        unsigned agc_gain_step1:6;
        unsigned agc_gain_step2:6;
        unsigned agc_a_filter_coef1:3;
        unsigned agc_a_filter_coef2:3;
        unsigned not_used:3;
    } b;
} dvbc_cfg_90_t;

typedef union dvbc_cfg_94 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned agc_target:4;
        unsigned agc_sd_rate:3;
        unsigned agc_cal_intv:2;
        unsigned agc_gain_step_if:1;
        unsigned agc_ifgain_freeze:1;
        unsigned agc_if_only:1;
        unsigned agc_iffb_set:1;
        unsigned agc_rfgain_freeze:1;
        unsigned agc_tuning_slope:1;
        unsigned agc_rffb_set:1;
        unsigned iffb_gain_sat_i:8;
        unsigned iffb_gain_sat:8;
    } b;
} dvbc_cfg_94_t;

typedef union dvbc_cfg_98 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned agc_rffb_value:11;
        unsigned agc_iffb_value:11;
        unsigned rffb_gain_sat:8;
        unsigned agc_gain_step_rf:1;
        unsigned dagc_pow_det:1;
    } b;
} dvbc_cfg_98_t;

typedef union dvbc_cfg_9c {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned agc_avg_power:9;
        unsigned agc_rffb_gain:11;
        unsigned agc_iffb_gain:11;
        unsigned not_used:1;
    } b;
} dvbc_cfg_9c_t;

typedef union dvbc_cfg_a0 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned dagc_gain_ctrl:7;
        unsigned dagc_hold:1;
        unsigned target_pow_p:6;
        unsigned dagc_bw:3;
        unsigned dagc_rstn:1;
        unsigned rffb_gain_sat_i:8;
        unsigned sw_agc_enable:1;
        unsigned adc_format:1;
        unsigned agc_da_sw:1;
        unsigned agc_gain_rate:3;
    } b;
} dvbc_cfg_a0_t;

typedef union dvbc_cfg_a4 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned dagc_gain_cnt:7;
        unsigned dagc_state:1;
        unsigned dagc_avg_pow:9;
        unsigned agc_stable:1;
        unsigned agc_in_target:1;
        unsigned reserved0:1;
        unsigned eq_state:4;
        unsigned not_used:8;
    } b;
} dvbc_cfg_a4_t;

typedef union dvbc_cfg_a8 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned cci1_b1:10;
        unsigned cci1_a2:10;
        unsigned cci1_a1:10;
        unsigned reserved:1;
        unsigned cci1_enable:1;
    } b;
} dvbc_cfg_a8_t;

typedef union dvbc_cfg_ac {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned cci2_b1:10;
        unsigned cci2_a2:10;
        unsigned cci2_a1:10;
        unsigned reserved:1;
        unsigned cci2_enable:1;
    } b;
} dvbc_cfg_ac_t;

typedef union dvbc_cfg_b0 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned test:32;
    } b;
} dvbc_cfg_b0_t;

typedef union dvbc_cfg_b4 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_cr_angle:30;
        unsigned not_used:2;
    } b;
} dvbc_cfg_b4_t;

typedef union dvbc_cfg_b8 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned tim_acc_pc0:32;
    } b;
} dvbc_cfg_b8_t;

typedef union dvbc_cfg_bc {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned reserved:32;
    } b;
} dvbc_cfg_bc_t;

typedef union dvbc_cfg_c0 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned fec_lost_th:4;
        unsigned eq_mma_th:4;
        unsigned search_symbol_rate_th:4;
        unsigned agc_stable_th:4;
        unsigned fine_symbol_rate_th:4;
        unsigned eq_smma_th:4;
        unsigned fec_lost_th_smma:4;
        unsigned reserved:4;
    } b;
} dvbc_cfg_c0_t;

typedef union dvbc_cfg_c4 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned state_probe:32;
    } b;
} dvbc_cfg_c4_t;

typedef union dvbc_cfg_c8 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned hw_symbol_rate:16;
        unsigned sync_fail:1;
        unsigned dc_offset:10;
        unsigned reserved:5;
    } b;
} dvbc_cfg_c8_t;

typedef union dvbc_cfg_cc {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned err_occur_cnt:16;
        unsigned fec_eq_mma_cnt:8;
        unsigned reserved:8;
    } b;
} dvbc_cfg_cc_t;

typedef union dvbc_cfg_d0 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned int_mask:32;
    } b;
} dvbc_cfg_d0_t;

typedef union dvbc_cfg_d4 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned int_status:32;
    } b;
} dvbc_cfg_d4_t;

typedef union dvbc_cfg_d8 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_coefq_max:16;
        unsigned eq_coefi_max:16;
    } b;
} dvbc_cfg_d8_t;

typedef union dvbc_cfg_dc {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned sum_phase_err_report:32;
    } b;
} dvbc_cfg_dc_t;

typedef union dvbc_cfg_e0 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_angle:30;
        unsigned reserved:2;
    } b;
} dvbc_cfg_e0_t;

typedef union dvbc_cfg_e4 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned segm_cnt:16;
        unsigned symb_cnt_eq:16;
    } b;
} dvbc_cfg_e4_t;

typedef union dvbc_cfg_e8 {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned sw_eq_smma_reset_n:1;
        unsigned err_occur_rst:1;
        unsigned demod_enable:1;
        unsigned soft_trigger_h9:9;
    } b;
} dvbc_cfg_e8_t;

typedef union dvbc_cfg_ec {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned eq_restore_angle:30;
        unsigned reserved:2;
    } b;
} dvbc_cfg_ec_t;

typedef struct atsc_cfg {
    int adr;
    int dat;
    int rw;
} atsc_cfg_t;

struct agc_power_tab {
	char name[128];
	int level;
	int ncalcE;
	int *calcE;
};

typedef struct dtmb_cfg {
    int dat;
    int adr;
    int rw;
} dtmb_cfg_t;



void demod_reset(void);
void demod_set_irq_mask(void);
void demod_clr_irq_stat(void);
void demod_set_adc_core_clk(int, int, int);
void demod_set_adc_core_clk_fix(int clk_adc, int clk_dem);
void calculate_cordic_para(void);
void ofdm_read_all_regs(void);
void demod_set_adc_core_clk_quick(int clk_adc_cfg, int clk_dem_cfg);


#endif
