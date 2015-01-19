#ifndef __SARADC_REG_H
#define __SARADC_REG_H

#include <mach/am_regs.h>

#define set_bits	WRITE_CBUS_REG_BITS
#define get_bits	READ_CBUS_REG_BITS
#define set_reg	WRITE_CBUS_REG
#define get_reg	READ_CBUS_REG

// REG0
#define detect_level()				get_bits(SAR_ADC_REG0, 31, 1)
#define delta_busy()					get_bits(SAR_ADC_REG0, 30, 1)
#define avg_busy()					get_bits(SAR_ADC_REG0, 29, 1)
#define sample_busy()				get_bits(SAR_ADC_REG0, 28, 1)
#define fifo_full()					get_bits(SAR_ADC_REG0, 27 1)
#define fifo_empty()					get_bits(SAR_ADC_REG0, 26, 1)
#define get_fifo_cnt()				get_bits(SAR_ADC_REG0, 21, 5)
#define get_cur_chan_id()			get_bits(SAR_ADC_REG0, 16, 3)
#define temp_sens_sel(sel)		set_bits(SAR_ADC_REG0, sel, 15, 1)
#define stop_sample()				set_bits(SAR_ADC_REG0, 1, 14, 1)

#define enable_chan1_delta()			set_bits(SAR_ADC_REG0, 1, 13, 1)
#define disable_chan1_delta()			set_bits(SAR_ADC_REG0, 0, 13, 1)
#define enable_chan0_delta()			set_bits(SAR_ADC_REG0, 1, 12, 1)
#define disable_chan0_delta()			set_bits(SAR_ADC_REG0, 0, 12, 1)

#define set_detect_irq_pol(pol)		set_bits(SAR_ADC_REG0, pol, 10, 1)
#define enable_detect_irq()			set_bits(SAR_ADC_REG0, 1, 9, 1)
#define disable_detect_irq()			set_bits(SAR_ADC_REG0, 0, 9, 1)

#define set_fifo_irq_count(cnt)			set_bits(SAR_ADC_REG0, cnt, 4, 5)
#define enable_fifo_irq()				set_bits(SAR_ADC_REG0, 1, 3, 1)
#define disable_fifo_irq()				set_bits(SAR_ADC_REG0, 0, 3, 1)

#define start_sample()				set_bits(SAR_ADC_REG0, 1, 2, 1)

#define enable_continuous_sample()	set_bits(SAR_ADC_REG0, 1, 1, 1)
#define disable_continuous_sample()	set_bits(SAR_ADC_REG0, 0, 1, 1)

#define enable_sample_engine()		set_bits(SAR_ADC_REG0, 1, 0, 1)
#define disable_sample_engine()		set_bits(SAR_ADC_REG0, 0, 0, 1)

// REG1
/* set_chan_list() - set  the channels list
 * @list: channels list to be process
 * @len: length of the list of channels to process */
#define set_chan_list(list, len)	set_reg(SAR_ADC_CHAN_LIST, list | ((len-1)<<24))

// REG2
enum {
	NO_AVG_MODE = 0,
	SIMPLE_AVG_MODE,
	MEDIAN_AVG_MODE,
};
enum {
	SAMPLE_NUM_1 = 0,
	SAMPLE_NUM_2,
	SAMPLE_NUM_4,
	SAMPLE_NUM_8
};
#define set_avg_mode(ch, mode, num) do {\
	set_bits(SAR_ADC_AVG_CNTL, num, ch*2, 2);\
	set_bits(SAR_ADC_AVG_CNTL, mode, ch*2+16, 2);\
} while(0)


// REG3
/* After all channels in the CHANNEL_LIST have been processed, the sampling engine
    will delay for an amount of time before re-processing the CHANNEL_LIST again */
enum {
	BLOCK_DELAY_TB_1US = 0,	/* count 1us ticks */
	BLOCK_DELAY_TB_10US,	/* count 10us ticks */
	BLOCK_DELAY_TB_100US,	/* count 100us ticks */
	BLOCK_DELAY_TB_1MS,	/* count 1ms ticks */
};
#define set_block_delay(delay, timebase) do {\
	set_bits(SAR_ADC_REG3, delay, 0, 8);\
	set_bits(SAR_ADC_REG3, timebase, 8, 2);\
} while(0)

/* The ADC clock is derived by dividing the 27Mhz crystal by N+1.
   This value divides the 27Mhz clock to generate an ADC clock.
   A value of 20 for example divides the 27Mhz clock by 21 to generate
   an equivalent 1.28Mhz clock */
#define set_clock_divider(div)	set_bits(SAR_ADC_REG3, div, 10, 6)

/* pannel detect signal filter's parameter */
enum {
	FILTER_TB_1US = 0,	/* count 1us ticks */
	FILTER_TB_10US,		/* count 10us ticks */
	FILTER_TB_100US,	/* count 100us ticks */
	FILTER_TB_1MS,		/* count 1ms ticks */
};
#define set_pannel_detect_filter(count, timebase) do {\
	set_bits(SAR_ADC_REG3, timebase, 16, 2);\
	set_bits(SAR_ADC_REG3, count, 18, 3);\
} while(0)

/* Enable/disable ADC */
#define enable_adc()	set_bits(SAR_ADC_REG3, 1, 21, 1)
#define disable_adc()	set_bits(SAR_ADC_REG3, 0, 21, 1)

/* controls the analog switch that connects a 50k resistor to the X+ signal.
   Setting this bit to 1 closes the analog switch */
#define enable_detect_pullup()		set_bits(SAR_ADC_REG3, 1, 22, 1)
#define disable_detect_pullup()	set_bits(SAR_ADC_REG3, 0, 22, 1)

/* ADC AINC sample mode */
enum {
	DIFF_MODE = 0,			/* differential mode */
	SINGLE_ENDED_MODE,	/* single ended */
};
#define set_sample_mode(mode)	set_bits(SAR_ADC_REG3, mode, 23, 1)

/* ADC Calibration resistor divider selection */
enum {
	CAL_VOLTAGE_1,	/* VSS */
	CAL_VOLTAGE_2,	/* VDD * 1/4 */
	CAL_VOLTAGE_3,	/* VDD * 2/4 */
	CAL_VOLTAGE_4,	/* VDD * 3/4 */
	CAL_VOLTAGE_5,	/* VDD */
	INTERNAL_CAL_NUM,
};
#define set_cal_voltage(sel)	set_bits(SAR_ADC_REG3, sel, 23, 3)

/* TEMPSEN_PD12, TEMPSEN_MODE */
#define set_tempsen(val)	set_bits(SAR_ADC_REG3, val, 28, 2)

/* enable/disable  the SAR ADC clock */
#define enable_clock()	set_bits(SAR_ADC_REG3, 1, 30, 1)
#define disable_clock()	set_bits(SAR_ADC_REG3, 0, 30, 1)

#define set_sc_phase() set_bits(SAR_ADC_REG3, 1, 26, 1)

// REG4
/* set_input_delay() - set input delay
 * As the CHANNEL_LIST is process, input switches should be set according to
 * the requirements of the channel,  After setting the switches there is a programmable
 * delay before sampling begins. */
enum {
	INPUT_DELAY_TB_110NS = 0,	/* count 110ns ticks */
	INPUT_DELAY_TB_1US,			/* count 1us ticks */
	INPUT_DELAY_TB_10US,		/* count 10us ticks */
	INPUT_DELAY_TB_100US,		/* count 100us ticks */
};
#define set_input_delay(delay, timebase) do {\
	set_bits(SAR_ADC_DELAY, delay, 16, 8);\
	set_bits(SAR_ADC_DELAY, timebase, 24, 2);\
} while(0)

/* set_sample_delay() - set sample delay
 * For channels that acquire 2,4 or 8 samples, there is a delay between two samples */
enum {
	SAMPLE_DELAY_TB_1US = 0,	/* count 1us ticks */
	SAMPLE_DELAY_TB_10US,		/* count 10us ticks */
	SAMPLE_DELAY_TB_100US,	/* count 100us ticks */
	SAMPLE_DELAY_TB_1MS,		/* count 1ms ticks */
};
#define set_sample_delay(delay, timebase) do {\
	set_bits(SAR_ADC_DELAY, delay, 0, 8);\
	set_bits(SAR_ADC_DELAY, timebase, 8, 2);\
} while(0)


// REG5
#define get_last_sample()	get_reg(SAR_ADC_LAST_RD)
// REG6
#define get_fifo_sample()		get_reg(SAR_ADC_FIFO_RD)

// REG7 = SAR_ADC_AUX_SW
// REG8 = SAR_ADC_CHAN_10_SW
#define set_sample_sw(ch, sw) do {\
	if (ch < 2)\
		set_bits(SAR_ADC_CHAN_10_SW, sw, ch*16, 7);\
	else\
		set_bits(SAR_ADC_AUX_SW, sw, 0, 7);\
} while(0)

#define set_sample_mux(ch, mux) do {\
	if (ch < 2)\
		set_bits(SAR_ADC_CHAN_10_SW, mux, ch*16 + 7, 3);\
	else\
		set_bits(SAR_ADC_AUX_SW, mux, (ch-2) * 3 + 8, 3);\
} while(0)

// REG9
#define set_idle_sw(sw)		set_bits(SAR_ADC_DETECT_IDLE_SW, sw, 0, 7)
#define set_idle_mux(mux)	set_bits(SAR_ADC_DETECT_IDLE_SW, mux, 7, 3)
#define set_detect_sw(sw)	set_bits(SAR_ADC_DETECT_IDLE_SW, sw, 16, 7)
#define set_detect_mux( mux)	set_bits(SAR_ADC_DETECT_IDLE_SW, mux, 23, 3)
#define enable_detect_sw()	set_bits(SAR_ADC_DETECT_IDLE_SW, 1, 26, 1)
#define disable_detect_sw()	set_bits(SAR_ADC_DETECT_IDLE_SW, 0, 26, 1)

// REG10
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define enable_bandgap()    set_bits(SAR_ADC_DELTA_10, 1, 10, 1)
#define disable_bandgap()   set_bits(SAR_ADC_DELTA_10, 0, 10, 1)
#define set_trimming(x)     set_bits(SAR_ADC_DELTA_10, x, 11, 4)
#define enable_temp__()     set_bits(SAR_ADC_DELTA_10, 1, 15, 1)
#define disable_temp__()    set_bits(SAR_ADC_DELTA_10, 0, 15, 1)
#define enable_temp()       set_bits(SAR_ADC_DELTA_10, 1, 26, 1)
#define disable_temp()      set_bits(SAR_ADC_DELTA_10, 0, 26, 1)
#define select_temp()       set_bits(SAR_ADC_DELTA_10, 1, 27, 1)
#define unselect_temp()     set_bits(SAR_ADC_DELTA_10, 0, 27, 1)
#endif
#define set_trimming1(x)     set_bits(HHI_DPLL_TOP_0, x, 9, 1)
#define XN_OFF		(0<<0)
#define XN_ON		(1<<0)
#define YN_OFF		(0<<1)
#define YN_ON		(1<<1)
#define XP_OFF		(1<<2)
#define XP_ON		(0<<2)
#define YP_OFF		(1<<3)
#define YP_ON		(0<<3)

#define MODE_SEL(sel)	(sel<<4)
#define VREF_N_MUX(mux)	(mux<<5)
#define VREF_P_MUX(mux)	(mux<<6)

#define IDLE_SW		(XP_OFF | XN_OFF | YP_OFF | YN_OFF\
					| MODE_SEL(0) | VREF_N_MUX(0) | VREF_P_MUX(0))
#define DETECT_SW	(XP_OFF | XN_OFF | YP_OFF | YN_ON\
					| MODE_SEL(0) | VREF_N_MUX(0) | VREF_P_MUX(0))
#define X_SW			(XP_ON | XN_ON | YP_OFF | YN_OFF\
					| MODE_SEL(0) | VREF_N_MUX(0) | VREF_P_MUX(0))
#define Y_SW			(XP_OFF | XN_OFF | YP_ON | YN_ON\
					| MODE_SEL(0) | VREF_N_MUX(0) | VREF_P_MUX(0))
#define Z1_SW		(XP_OFF | XN_ON | YP_ON | YN_OFF\
					| MODE_SEL(0) | VREF_N_MUX(0) | VREF_P_MUX(0))
#define Z2_SW		(Z1_SW)

#endif
