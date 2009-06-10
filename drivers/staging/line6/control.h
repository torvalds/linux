/*
 * Line6 Linux USB driver - 0.8.0
 *
 * Copyright (C) 2004-2009 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#ifndef LINE6_CONTROL_H
#define LINE6_CONTROL_H


/**
   List of PODxt Pro controls.
   See Appendix C of the "PODxt (Pro) Pilot's Handbook" by Line6.
   Comments after the number refer to the PODxt Pro firmware version required
   for this feature.
*/
enum {
	POD_tweak                          =   1,
	POD_wah_position                   =   4,
	POD_compression_gain               =   5,  /* device: LINE6_BITS_PODXTALL */
	POD_vol_pedal_position             =   7,
	POD_compression_threshold          =   9,
	POD_pan                            =  10,
	POD_amp_model_setup                =  11,
	POD_amp_model                      =  12,  /* firmware: 2.0 */
	POD_drive                          =  13,
	POD_bass                           =  14,
	POD_mid                            =  15,  /* device: LINE6_BITS_PODXTALL */
	POD_lowmid                         =  15,  /* device: LINE6_BITS_BASSPODXTALL */
	POD_treble                         =  16,  /* device: LINE6_BITS_PODXTALL */
	POD_highmid                        =  16,  /* device: LINE6_BITS_BASSPODXTALL */
	POD_chan_vol                       =  17,
	POD_reverb_mix                     =  18,  /* device: LINE6_BITS_PODXTALL */
	POD_effect_setup                   =  19,
	POD_band_1_frequency               =  20,  /* firmware: 2.0 */
	POD_presence                       =  21,  /* device: LINE6_BITS_PODXTALL */
	POD_treble__bass                   =  21,  /* device: LINE6_BITS_BASSPODXTALL */
	POD_noise_gate_enable              =  22,
	POD_gate_threshold                 =  23,
	POD_gate_decay_time                =  24,
	POD_stomp_enable                   =  25,
	POD_comp_enable                    =  26,
	POD_stomp_time                     =  27,
	POD_delay_enable                   =  28,
	POD_mod_param_1                    =  29,
	POD_delay_param_1                  =  30,
	POD_delay_param_1_note_value       =  31,
	POD_band_2_frequency__bass         =  32,  /* device: LINE6_BITS_BASSPODXTALL */ /* firmware: 2.0 */
	POD_delay_param_2                  =  33,
	POD_delay_volume_mix               =  34,
	POD_delay_param_3                  =  35,
	POD_reverb_enable                  =  36,  /* device: LINE6_BITS_PODXTALL */
	POD_reverb_type                    =  37,  /* device: LINE6_BITS_PODXTALL */
	POD_reverb_decay                   =  38,  /* device: LINE6_BITS_PODXTALL */
	POD_reverb_tone                    =  39,  /* device: LINE6_BITS_PODXTALL */
	POD_reverb_pre_delay               =  40,  /* device: LINE6_BITS_PODXTALL */
	POD_reverb_pre_post                =  41,  /* device: LINE6_BITS_PODXTALL */
	POD_band_2_frequency               =  42,  /* device: LINE6_BITS_PODXTALL */     /* firmware: 2.0 */
	POD_band_3_frequency__bass         =  42,  /* device: LINE6_BITS_BASSPODXTALL */ /* firmware: 2.0 */
	POD_wah_enable                     =  43,
	POD_modulation_lo_cut              =  44,  /* device: LINE6_BITS_BASSPODXTALL */
	POD_delay_reverb_lo_cut            =  45,  /* device: LINE6_BITS_BASSPODXTALL */
	POD_volume_pedal_minimum           =  46,  /* device: LINE6_BITS_PODXTALL */     /* firmware: 2.0 */
	POD_eq_pre_post                    =  46,  /* device: LINE6_BITS_BASSPODXTALL */ /* firmware: 2.0 */
	POD_volume_pre_post                =  47,
	POD_di_model                       =  48,  /* device: LINE6_BITS_BASSPODXTALL */
	POD_di_delay                       =  49,  /* device: LINE6_BITS_BASSPODXTALL */
	POD_mod_enable                     =  50,
	POD_mod_param_1_note_value         =  51,
	POD_mod_param_2                    =  52,
	POD_mod_param_3                    =  53,
	POD_mod_param_4                    =  54,
	POD_mod_param_5                    =  55,  /* device: LINE6_BITS_BASSPODXTALL */
	POD_mod_volume_mix                 =  56,
	POD_mod_pre_post                   =  57,
	POD_modulation_model               =  58,
	POD_band_3_frequency               =  60,  /* device: LINE6_BITS_PODXTALL */     /* firmware: 2.0 */
	POD_band_4_frequency__bass         =  60,  /* device: LINE6_BITS_BASSPODXTALL */ /* firmware: 2.0 */
	POD_mod_param_1_double_precision   =  61,
	POD_delay_param_1_double_precision =  62,
	POD_eq_enable                      =  63,  /* firmware: 2.0 */
	POD_tap                            =  64,
	POD_volume_tweak_pedal_assign      =  65,
	POD_band_5_frequency               =  68,  /* device: LINE6_BITS_BASSPODXTALL */ /* firmware: 2.0 */
	POD_tuner                          =  69,
	POD_mic_selection                  =  70,
	POD_cabinet_model                  =  71,
	POD_stomp_model                    =  75,
	POD_roomlevel                      =  76,
	POD_band_4_frequency               =  77,  /* device: LINE6_BITS_PODXTALL */     /* firmware: 2.0 */
	POD_band_6_frequency               =  77,  /* device: LINE6_BITS_BASSPODXTALL */ /* firmware: 2.0 */
	POD_stomp_param_1_note_value       =  78,
	POD_stomp_param_2                  =  79,
	POD_stomp_param_3                  =  80,
	POD_stomp_param_4                  =  81,
	POD_stomp_param_5                  =  82,
	POD_stomp_param_6                  =  83,
	POD_amp_switch_select              =  84,  /* device: LINE6_BITS_LIVE */
	POD_delay_param_4                  =  85,
	POD_delay_param_5                  =  86,
	POD_delay_pre_post                 =  87,
	POD_delay_model                    =  88,  /* device: LINE6_BITS_PODXTALL */
	POD_delay_verb_model               =  88,  /* device: LINE6_BITS_BASSPODXTALL */
	POD_tempo_msb                      =  89,
	POD_tempo_lsb                      =  90,
	POD_wah_model                      =  91,  /* firmware: 3.0 */
	POD_bypass_volume                  = 105,  /* firmware: 2.14 */
	POD_fx_loop_on_off                 = 107,  /* device: LINE6_BITS_PRO */
	POD_tweak_param_select             = 108,
	POD_amp1_engage                    = 111,
	POD_band_1_gain                    = 114,  /* firmware: 2.0 */
	POD_band_2_gain__bass              = 115,  /* device: LINE6_BITS_BASSPODXTALL */ /* firmware: 2.0 */
	POD_band_2_gain                    = 116,  /* device: LINE6_BITS_PODXTALL */     /* firmware: 2.0 */
	POD_band_3_gain__bass              = 116,  /* device: LINE6_BITS_BASSPODXTALL */ /* firmware: 2.0 */
	POD_band_3_gain                    = 117,  /* device: LINE6_BITS_PODXTALL */     /* firmware: 2.0 */
	POD_band_4_gain__bass              = 117,  /* device: LINE6_BITS_BASSPODXTALL */ /* firmware: 2.0 */
	POD_band_5_gain__bass              = 118,  /* device: LINE6_BITS_BASSPODXTALL */ /* firmware: 2.0 */
	POD_band_4_gain                    = 119,  /* device: LINE6_BITS_PODXTALL */     /* firmware: 2.0 */
	POD_band_6_gain__bass              = 119   /* device: LINE6_BITS_BASSPODXTALL */ /* firmware: 2.0 */
};

/**
   List of Variax workbench controls (dump).
*/
enum {
	VARIAX_body                        =   3,
	VARIAX_pickup1_enable              =   4,  /* 0: enabled, 1: disabled */
	VARIAX_pickup1_type                =   8,
	VARIAX_pickup1_position            =   9,  /* type: 24 bit float */
	VARIAX_pickup1_angle               =  12,  /* type: 24 bit float */
	VARIAX_pickup1_level               =  15,  /* type: 24 bit float */
	VARIAX_pickup2_enable              =  18,  /* 0: enabled, 1: disabled */
	VARIAX_pickup2_type                =  22,
	VARIAX_pickup2_position            =  23,  /* type: 24 bit float */
	VARIAX_pickup2_angle               =  26,  /* type: 24 bit float */
	VARIAX_pickup2_level               =  29,  /* type: 24 bit float */
	VARIAX_pickup_phase                =  32,  /* 0: in phase, 1: out of phase */
	VARIAX_capacitance                 =  33,  /* type: 24 bit float */
	VARIAX_tone_resistance             =  36,  /* type: 24 bit float */
	VARIAX_volume_resistance           =  39,  /* type: 24 bit float */
	VARIAX_taper                       =  42,  /* 0: Linear, 1: Audio */
	VARIAX_tone_dump                   =  43,  /* type: 24 bit float */
	VARIAX_save_tone                   =  46,
	VARIAX_volume_dump                 =  47,  /* type: 24 bit float */
	VARIAX_tuning_enable               =  50,
	VARIAX_tuning6                     =  51,
	VARIAX_tuning5                     =  52,
	VARIAX_tuning4                     =  53,
	VARIAX_tuning3                     =  54,
	VARIAX_tuning2                     =  55,
	VARIAX_tuning1                     =  56,
	VARIAX_detune6                     =  57,  /* type: 24 bit float */
	VARIAX_detune5                     =  60,  /* type: 24 bit float */
	VARIAX_detune4                     =  63,  /* type: 24 bit float */
	VARIAX_detune3                     =  66,  /* type: 24 bit float */
	VARIAX_detune2                     =  69,  /* type: 24 bit float */
	VARIAX_detune1                     =  72,  /* type: 24 bit float */
	VARIAX_mix6                        =  75,  /* type: 24 bit float */
	VARIAX_mix5                        =  78,  /* type: 24 bit float */
	VARIAX_mix4                        =  81,  /* type: 24 bit float */
	VARIAX_mix3                        =  84,  /* type: 24 bit float */
	VARIAX_mix2                        =  87,  /* type: 24 bit float */
	VARIAX_mix1                        =  90,  /* type: 24 bit float */
	VARIAX_pickup_wiring               =  96   /* 0: parallel, 1: series */
};

/**
   List of Variax workbench controls (MIDI).
*/
enum {
	VARIAXMIDI_volume                  =   7,
	VARIAXMIDI_tone                    =  79,
};


extern int pod_create_files(int firmware, int type, struct device *dev);
extern void pod_remove_files(int firmware, int type, struct device *dev);
extern int variax_create_files(int firmware, int type, struct device *dev);
extern void variax_remove_files(int firmware, int type, struct device *dev);


#endif
