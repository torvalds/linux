#ifndef __INC_ADCSMP_H
#define __INC_ADCSMP_H

#define DYNAMIC_LA_MODE	"1.0"  /*2016.07.15  Dino */

#if (PHYDM_LA_MODE_SUPPORT == 1)

struct _RT_ADCSMP_STRING {
	u32		*octet;
	u32		length;
	u32		buffer_size;
	u32		start_pos;
};


enum rt_adcsmp_trig_sel {
	PHYDM_ADC_BB_TRIG	= 0,
	PHYDM_ADC_MAC_TRIG	= 1,
	PHYDM_ADC_RF0_TRIG	= 2,
	PHYDM_ADC_RF1_TRIG	= 3,
	PHYDM_MAC_TRIG		= 4
};


enum rt_adcsmp_trig_sig_sel {
	ADCSMP_TRIG_CRCOK	= 0,
	ADCSMP_TRIG_CRCFAIL	= 1,
	ADCSMP_TRIG_CCA		= 2,
	ADCSMP_TRIG_REG		= 3
};


enum rt_adcsmp_state {
	ADCSMP_STATE_IDLE		= 0,
	ADCSMP_STATE_SET		= 1,
	ADCSMP_STATE_QUERY	=	2
};


struct _RT_ADCSMP {
	struct _RT_ADCSMP_STRING		adc_smp_buf;
	enum rt_adcsmp_state		adc_smp_state;
	u8					la_trig_mode;
	u32					la_trig_sig_sel;
	u8					la_dma_type;
	u32					la_trigger_time;
	u32					la_mac_ref_mask;
	u32					la_dbg_port;
	u8					la_trigger_edge;
	u8					la_smp_rate;
	u32					la_count;
	u8					is_bb_trigger;
	u8					la_work_item_index;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	RT_WORK_ITEM	adc_smp_work_item;
	RT_WORK_ITEM	adc_smp_work_item_1;
#endif
};

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
void
adc_smp_work_item_callback(
	void	*p_context
);
#endif

void
adc_smp_set(
	void	*p_dm_void,
	u8	trig_mode,
	u32	trig_sig_sel,
	u8	dma_data_sig_sel,
	u32	trigger_time,
	u16	polling_time
);

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
enum rt_status
adc_smp_query(
	void	*p_dm_void,
	ULONG	information_buffer_length,
	void	*information_buffer,
	PULONG	bytes_written
);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
void
adc_smp_query(
	void		*p_dm_void,
	void		*output,
	u32		out_len,
	u32		*pused
);

s32
adc_smp_get_sample_counts(
	void		*p_dm_void
);

s32
adc_smp_query_single_data(
	void		*p_dm_void,
	void		*output,
	u32		out_len,
	u32		index
);

#endif
void
adc_smp_stop(
	void	*p_dm_void
);

void
adc_smp_init(
	void	*p_dm_void
);

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
void
adc_smp_de_init(
	void			*p_dm_void
);
#endif

void
phydm_la_mode_bb_setting(
	void		*p_dm_void
);

void
phydm_la_mode_set_trigger_time(
	void		*p_dm_void,
	u32		trigger_time_mu_sec
);

void
phydm_lamode_trigger_setting(
	void		*p_dm_void,
	char			input[][16],
	u32		*_used,
	char			*output,
	u32		*_out_len,
	u32		input_num
);
#endif
#endif
