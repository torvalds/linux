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

#include "driver.h"

#include <linux/usb.h>

#include "control.h"
#include "pod.h"
#include "usbdefs.h"
#include "variax.h"

#define DEVICE_ATTR2(_name1, _name2, _mode, _show, _store) \
struct device_attribute dev_attr_##_name1 = __ATTR(_name2, _mode, _show, _store)

#define LINE6_PARAM_R(PREFIX, prefix, type, param) \
static ssize_t prefix ## _get_ ## param(struct device *dev, \
			struct device_attribute *attr, char *buf) \
{ \
	return prefix ## _get_param_ ## type(dev, buf, PREFIX ## _ ## param); \
}

#define LINE6_PARAM_RW(PREFIX, prefix, type, param) \
LINE6_PARAM_R(PREFIX, prefix, type, param); \
static ssize_t prefix ## _set_ ## param(struct device *dev, \
		struct device_attribute *attr, const char *buf, size_t count) \
{ \
	return prefix ## _set_param_ ## type(dev, buf, count, PREFIX ## _ ## param); \
}

#define POD_PARAM_R(type, param) LINE6_PARAM_R(POD, pod, type, param)
#define POD_PARAM_RW(type, param) LINE6_PARAM_RW(POD, pod, type, param)
#define VARIAX_PARAM_R(type, param) LINE6_PARAM_R(VARIAX, variax, type, param)
#define VARIAX_PARAM_RW(type, param) LINE6_PARAM_RW(VARIAX, variax, type, param)


static ssize_t pod_get_param_int(struct device *dev, char *buf, int param)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	int retval = line6_wait_dump(&pod->dumpreq, 0);
	if (retval < 0)
		return retval;
	return sprintf(buf, "%d\n", pod->prog_data.control[param]);
}

static ssize_t pod_set_param_int(struct device *dev, const char *buf, size_t count, int param)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	int value = simple_strtoul(buf, NULL, 10);
	pod_transmit_parameter(pod, param, value);
	return count;
}

static ssize_t variax_get_param_int(struct device *dev, char *buf, int param)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_variax *variax = usb_get_intfdata(interface);
	int retval = line6_wait_dump(&variax->dumpreq, 0);
	if (retval < 0)
		return retval;
	return sprintf(buf, "%d\n", variax->model_data.control[param]);
}

static ssize_t variax_get_param_float(struct device *dev, char *buf, int param)
{
	/*
		We do our own floating point handling here since floats in the
		kernel are problematic for at least two reasons: - many distros
		are still shipped with binary kernels optimized for the ancient
		80386 without FPU
		- there isn't a printf("%f")
		  (see http://www.kernelthread.com/publications/faq/335.html)
	*/

	static const int BIAS = 0x7f;
	static const int OFFSET = 0xf;
	static const int PRECISION = 1000;

	int len = 0;
	unsigned part_int, part_frac;
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_variax *variax = usb_get_intfdata(interface);
	const unsigned char *p = variax->model_data.control + param;
	int retval = line6_wait_dump(&variax->dumpreq, 0);
	if (retval < 0)
		return retval;

	if ((p[0] == 0) && (p[1] == 0) && (p[2] == 0))
		part_int = part_frac = 0;
	else {
		int exponent = (((p[0] & 0x7f) << 1) | (p[1] >> 7)) - BIAS;
		unsigned mantissa = (p[1] << 8) | p[2] | 0x8000;
		exponent -= OFFSET;

		if (exponent >= 0) {
			part_int = mantissa << exponent;
			part_frac = 0;
		} else {
			part_int = mantissa >> -exponent;
			part_frac = (mantissa << (32 + exponent)) & 0xffffffff;
		}

		part_frac = (part_frac / ((1UL << 31) / (PRECISION / 2 * 10)) + 5) / 10;
	}

	len += sprintf(buf + len, "%s%d.%03d\n", ((p[0] & 0x80) ? "-" : ""), part_int, part_frac);
	return len;
}

POD_PARAM_RW(int, tweak);
POD_PARAM_RW(int, wah_position);
POD_PARAM_RW(int, compression_gain);
POD_PARAM_RW(int, vol_pedal_position);
POD_PARAM_RW(int, compression_threshold);
POD_PARAM_RW(int, pan);
POD_PARAM_RW(int, amp_model_setup);
POD_PARAM_RW(int, amp_model);
POD_PARAM_RW(int, drive);
POD_PARAM_RW(int, bass);
POD_PARAM_RW(int, mid);
POD_PARAM_RW(int, lowmid);
POD_PARAM_RW(int, treble);
POD_PARAM_RW(int, highmid);
POD_PARAM_RW(int, chan_vol);
POD_PARAM_RW(int, reverb_mix);
POD_PARAM_RW(int, effect_setup);
POD_PARAM_RW(int, band_1_frequency);
POD_PARAM_RW(int, presence);
POD_PARAM_RW(int, treble__bass);
POD_PARAM_RW(int, noise_gate_enable);
POD_PARAM_RW(int, gate_threshold);
POD_PARAM_RW(int, gate_decay_time);
POD_PARAM_RW(int, stomp_enable);
POD_PARAM_RW(int, comp_enable);
POD_PARAM_RW(int, stomp_time);
POD_PARAM_RW(int, delay_enable);
POD_PARAM_RW(int, mod_param_1);
POD_PARAM_RW(int, delay_param_1);
POD_PARAM_RW(int, delay_param_1_note_value);
POD_PARAM_RW(int, band_2_frequency__bass);
POD_PARAM_RW(int, delay_param_2);
POD_PARAM_RW(int, delay_volume_mix);
POD_PARAM_RW(int, delay_param_3);
POD_PARAM_RW(int, reverb_enable);
POD_PARAM_RW(int, reverb_type);
POD_PARAM_RW(int, reverb_decay);
POD_PARAM_RW(int, reverb_tone);
POD_PARAM_RW(int, reverb_pre_delay);
POD_PARAM_RW(int, reverb_pre_post);
POD_PARAM_RW(int, band_2_frequency);
POD_PARAM_RW(int, band_3_frequency__bass);
POD_PARAM_RW(int, wah_enable);
POD_PARAM_RW(int, modulation_lo_cut);
POD_PARAM_RW(int, delay_reverb_lo_cut);
POD_PARAM_RW(int, volume_pedal_minimum);
POD_PARAM_RW(int, eq_pre_post);
POD_PARAM_RW(int, volume_pre_post);
POD_PARAM_RW(int, di_model);
POD_PARAM_RW(int, di_delay);
POD_PARAM_RW(int, mod_enable);
POD_PARAM_RW(int, mod_param_1_note_value);
POD_PARAM_RW(int, mod_param_2);
POD_PARAM_RW(int, mod_param_3);
POD_PARAM_RW(int, mod_param_4);
POD_PARAM_RW(int, mod_param_5);
POD_PARAM_RW(int, mod_volume_mix);
POD_PARAM_RW(int, mod_pre_post);
POD_PARAM_RW(int, modulation_model);
POD_PARAM_RW(int, band_3_frequency);
POD_PARAM_RW(int, band_4_frequency__bass);
POD_PARAM_RW(int, mod_param_1_double_precision);
POD_PARAM_RW(int, delay_param_1_double_precision);
POD_PARAM_RW(int, eq_enable);
POD_PARAM_RW(int, tap);
POD_PARAM_RW(int, volume_tweak_pedal_assign);
POD_PARAM_RW(int, band_5_frequency);
POD_PARAM_RW(int, tuner);
POD_PARAM_RW(int, mic_selection);
POD_PARAM_RW(int, cabinet_model);
POD_PARAM_RW(int, stomp_model);
POD_PARAM_RW(int, roomlevel);
POD_PARAM_RW(int, band_4_frequency);
POD_PARAM_RW(int, band_6_frequency);
POD_PARAM_RW(int, stomp_param_1_note_value);
POD_PARAM_RW(int, stomp_param_2);
POD_PARAM_RW(int, stomp_param_3);
POD_PARAM_RW(int, stomp_param_4);
POD_PARAM_RW(int, stomp_param_5);
POD_PARAM_RW(int, stomp_param_6);
POD_PARAM_RW(int, amp_switch_select);
POD_PARAM_RW(int, delay_param_4);
POD_PARAM_RW(int, delay_param_5);
POD_PARAM_RW(int, delay_pre_post);
POD_PARAM_RW(int, delay_model);
POD_PARAM_RW(int, delay_verb_model);
POD_PARAM_RW(int, tempo_msb);
POD_PARAM_RW(int, tempo_lsb);
POD_PARAM_RW(int, wah_model);
POD_PARAM_RW(int, bypass_volume);
POD_PARAM_RW(int, fx_loop_on_off);
POD_PARAM_RW(int, tweak_param_select);
POD_PARAM_RW(int, amp1_engage);
POD_PARAM_RW(int, band_1_gain);
POD_PARAM_RW(int, band_2_gain__bass);
POD_PARAM_RW(int, band_2_gain);
POD_PARAM_RW(int, band_3_gain__bass);
POD_PARAM_RW(int, band_3_gain);
POD_PARAM_RW(int, band_4_gain__bass);
POD_PARAM_RW(int, band_5_gain__bass);
POD_PARAM_RW(int, band_4_gain);
POD_PARAM_RW(int, band_6_gain__bass);
VARIAX_PARAM_R(int, body);
VARIAX_PARAM_R(int, pickup1_enable);
VARIAX_PARAM_R(int, pickup1_type);
VARIAX_PARAM_R(float, pickup1_position);
VARIAX_PARAM_R(float, pickup1_angle);
VARIAX_PARAM_R(float, pickup1_level);
VARIAX_PARAM_R(int, pickup2_enable);
VARIAX_PARAM_R(int, pickup2_type);
VARIAX_PARAM_R(float, pickup2_position);
VARIAX_PARAM_R(float, pickup2_angle);
VARIAX_PARAM_R(float, pickup2_level);
VARIAX_PARAM_R(int, pickup_phase);
VARIAX_PARAM_R(float, capacitance);
VARIAX_PARAM_R(float, tone_resistance);
VARIAX_PARAM_R(float, volume_resistance);
VARIAX_PARAM_R(int, taper);
VARIAX_PARAM_R(float, tone_dump);
VARIAX_PARAM_R(int, save_tone);
VARIAX_PARAM_R(float, volume_dump);
VARIAX_PARAM_R(int, tuning_enable);
VARIAX_PARAM_R(int, tuning6);
VARIAX_PARAM_R(int, tuning5);
VARIAX_PARAM_R(int, tuning4);
VARIAX_PARAM_R(int, tuning3);
VARIAX_PARAM_R(int, tuning2);
VARIAX_PARAM_R(int, tuning1);
VARIAX_PARAM_R(float, detune6);
VARIAX_PARAM_R(float, detune5);
VARIAX_PARAM_R(float, detune4);
VARIAX_PARAM_R(float, detune3);
VARIAX_PARAM_R(float, detune2);
VARIAX_PARAM_R(float, detune1);
VARIAX_PARAM_R(float, mix6);
VARIAX_PARAM_R(float, mix5);
VARIAX_PARAM_R(float, mix4);
VARIAX_PARAM_R(float, mix3);
VARIAX_PARAM_R(float, mix2);
VARIAX_PARAM_R(float, mix1);
VARIAX_PARAM_R(int, pickup_wiring);

static DEVICE_ATTR(tweak, S_IWUGO | S_IRUGO, pod_get_tweak, pod_set_tweak);
static DEVICE_ATTR(wah_position, S_IWUGO | S_IRUGO, pod_get_wah_position, pod_set_wah_position);
static DEVICE_ATTR(compression_gain, S_IWUGO | S_IRUGO, pod_get_compression_gain, pod_set_compression_gain);
static DEVICE_ATTR(vol_pedal_position, S_IWUGO | S_IRUGO, pod_get_vol_pedal_position, pod_set_vol_pedal_position);
static DEVICE_ATTR(compression_threshold, S_IWUGO | S_IRUGO, pod_get_compression_threshold, pod_set_compression_threshold);
static DEVICE_ATTR(pan, S_IWUGO | S_IRUGO, pod_get_pan, pod_set_pan);
static DEVICE_ATTR(amp_model_setup, S_IWUGO | S_IRUGO, pod_get_amp_model_setup, pod_set_amp_model_setup);
static DEVICE_ATTR(amp_model, S_IWUGO | S_IRUGO, pod_get_amp_model, pod_set_amp_model);
static DEVICE_ATTR(drive, S_IWUGO | S_IRUGO, pod_get_drive, pod_set_drive);
static DEVICE_ATTR(bass, S_IWUGO | S_IRUGO, pod_get_bass, pod_set_bass);
static DEVICE_ATTR(mid, S_IWUGO | S_IRUGO, pod_get_mid, pod_set_mid);
static DEVICE_ATTR(lowmid, S_IWUGO | S_IRUGO, pod_get_lowmid, pod_set_lowmid);
static DEVICE_ATTR(treble, S_IWUGO | S_IRUGO, pod_get_treble, pod_set_treble);
static DEVICE_ATTR(highmid, S_IWUGO | S_IRUGO, pod_get_highmid, pod_set_highmid);
static DEVICE_ATTR(chan_vol, S_IWUGO | S_IRUGO, pod_get_chan_vol, pod_set_chan_vol);
static DEVICE_ATTR(reverb_mix, S_IWUGO | S_IRUGO, pod_get_reverb_mix, pod_set_reverb_mix);
static DEVICE_ATTR(effect_setup, S_IWUGO | S_IRUGO, pod_get_effect_setup, pod_set_effect_setup);
static DEVICE_ATTR(band_1_frequency, S_IWUGO | S_IRUGO, pod_get_band_1_frequency, pod_set_band_1_frequency);
static DEVICE_ATTR(presence, S_IWUGO | S_IRUGO, pod_get_presence, pod_set_presence);
static DEVICE_ATTR2(treble__bass, treble, S_IWUGO | S_IRUGO, pod_get_treble__bass, pod_set_treble__bass);
static DEVICE_ATTR(noise_gate_enable, S_IWUGO | S_IRUGO, pod_get_noise_gate_enable, pod_set_noise_gate_enable);
static DEVICE_ATTR(gate_threshold, S_IWUGO | S_IRUGO, pod_get_gate_threshold, pod_set_gate_threshold);
static DEVICE_ATTR(gate_decay_time, S_IWUGO | S_IRUGO, pod_get_gate_decay_time, pod_set_gate_decay_time);
static DEVICE_ATTR(stomp_enable, S_IWUGO | S_IRUGO, pod_get_stomp_enable, pod_set_stomp_enable);
static DEVICE_ATTR(comp_enable, S_IWUGO | S_IRUGO, pod_get_comp_enable, pod_set_comp_enable);
static DEVICE_ATTR(stomp_time, S_IWUGO | S_IRUGO, pod_get_stomp_time, pod_set_stomp_time);
static DEVICE_ATTR(delay_enable, S_IWUGO | S_IRUGO, pod_get_delay_enable, pod_set_delay_enable);
static DEVICE_ATTR(mod_param_1, S_IWUGO | S_IRUGO, pod_get_mod_param_1, pod_set_mod_param_1);
static DEVICE_ATTR(delay_param_1, S_IWUGO | S_IRUGO, pod_get_delay_param_1, pod_set_delay_param_1);
static DEVICE_ATTR(delay_param_1_note_value, S_IWUGO | S_IRUGO, pod_get_delay_param_1_note_value, pod_set_delay_param_1_note_value);
static DEVICE_ATTR2(band_2_frequency__bass, band_2_frequency, S_IWUGO | S_IRUGO, pod_get_band_2_frequency__bass, pod_set_band_2_frequency__bass);
static DEVICE_ATTR(delay_param_2, S_IWUGO | S_IRUGO, pod_get_delay_param_2, pod_set_delay_param_2);
static DEVICE_ATTR(delay_volume_mix, S_IWUGO | S_IRUGO, pod_get_delay_volume_mix, pod_set_delay_volume_mix);
static DEVICE_ATTR(delay_param_3, S_IWUGO | S_IRUGO, pod_get_delay_param_3, pod_set_delay_param_3);
static DEVICE_ATTR(reverb_enable, S_IWUGO | S_IRUGO, pod_get_reverb_enable, pod_set_reverb_enable);
static DEVICE_ATTR(reverb_type, S_IWUGO | S_IRUGO, pod_get_reverb_type, pod_set_reverb_type);
static DEVICE_ATTR(reverb_decay, S_IWUGO | S_IRUGO, pod_get_reverb_decay, pod_set_reverb_decay);
static DEVICE_ATTR(reverb_tone, S_IWUGO | S_IRUGO, pod_get_reverb_tone, pod_set_reverb_tone);
static DEVICE_ATTR(reverb_pre_delay, S_IWUGO | S_IRUGO, pod_get_reverb_pre_delay, pod_set_reverb_pre_delay);
static DEVICE_ATTR(reverb_pre_post, S_IWUGO | S_IRUGO, pod_get_reverb_pre_post, pod_set_reverb_pre_post);
static DEVICE_ATTR(band_2_frequency, S_IWUGO | S_IRUGO, pod_get_band_2_frequency, pod_set_band_2_frequency);
static DEVICE_ATTR2(band_3_frequency__bass, band_3_frequency, S_IWUGO | S_IRUGO, pod_get_band_3_frequency__bass, pod_set_band_3_frequency__bass);
static DEVICE_ATTR(wah_enable, S_IWUGO | S_IRUGO, pod_get_wah_enable, pod_set_wah_enable);
static DEVICE_ATTR(modulation_lo_cut, S_IWUGO | S_IRUGO, pod_get_modulation_lo_cut, pod_set_modulation_lo_cut);
static DEVICE_ATTR(delay_reverb_lo_cut, S_IWUGO | S_IRUGO, pod_get_delay_reverb_lo_cut, pod_set_delay_reverb_lo_cut);
static DEVICE_ATTR(volume_pedal_minimum, S_IWUGO | S_IRUGO, pod_get_volume_pedal_minimum, pod_set_volume_pedal_minimum);
static DEVICE_ATTR(eq_pre_post, S_IWUGO | S_IRUGO, pod_get_eq_pre_post, pod_set_eq_pre_post);
static DEVICE_ATTR(volume_pre_post, S_IWUGO | S_IRUGO, pod_get_volume_pre_post, pod_set_volume_pre_post);
static DEVICE_ATTR(di_model, S_IWUGO | S_IRUGO, pod_get_di_model, pod_set_di_model);
static DEVICE_ATTR(di_delay, S_IWUGO | S_IRUGO, pod_get_di_delay, pod_set_di_delay);
static DEVICE_ATTR(mod_enable, S_IWUGO | S_IRUGO, pod_get_mod_enable, pod_set_mod_enable);
static DEVICE_ATTR(mod_param_1_note_value, S_IWUGO | S_IRUGO, pod_get_mod_param_1_note_value, pod_set_mod_param_1_note_value);
static DEVICE_ATTR(mod_param_2, S_IWUGO | S_IRUGO, pod_get_mod_param_2, pod_set_mod_param_2);
static DEVICE_ATTR(mod_param_3, S_IWUGO | S_IRUGO, pod_get_mod_param_3, pod_set_mod_param_3);
static DEVICE_ATTR(mod_param_4, S_IWUGO | S_IRUGO, pod_get_mod_param_4, pod_set_mod_param_4);
static DEVICE_ATTR(mod_param_5, S_IWUGO | S_IRUGO, pod_get_mod_param_5, pod_set_mod_param_5);
static DEVICE_ATTR(mod_volume_mix, S_IWUGO | S_IRUGO, pod_get_mod_volume_mix, pod_set_mod_volume_mix);
static DEVICE_ATTR(mod_pre_post, S_IWUGO | S_IRUGO, pod_get_mod_pre_post, pod_set_mod_pre_post);
static DEVICE_ATTR(modulation_model, S_IWUGO | S_IRUGO, pod_get_modulation_model, pod_set_modulation_model);
static DEVICE_ATTR(band_3_frequency, S_IWUGO | S_IRUGO, pod_get_band_3_frequency, pod_set_band_3_frequency);
static DEVICE_ATTR2(band_4_frequency__bass, band_4_frequency, S_IWUGO | S_IRUGO, pod_get_band_4_frequency__bass, pod_set_band_4_frequency__bass);
static DEVICE_ATTR(mod_param_1_double_precision, S_IWUGO | S_IRUGO, pod_get_mod_param_1_double_precision, pod_set_mod_param_1_double_precision);
static DEVICE_ATTR(delay_param_1_double_precision, S_IWUGO | S_IRUGO, pod_get_delay_param_1_double_precision, pod_set_delay_param_1_double_precision);
static DEVICE_ATTR(eq_enable, S_IWUGO | S_IRUGO, pod_get_eq_enable, pod_set_eq_enable);
static DEVICE_ATTR(tap, S_IWUGO | S_IRUGO, pod_get_tap, pod_set_tap);
static DEVICE_ATTR(volume_tweak_pedal_assign, S_IWUGO | S_IRUGO, pod_get_volume_tweak_pedal_assign, pod_set_volume_tweak_pedal_assign);
static DEVICE_ATTR(band_5_frequency, S_IWUGO | S_IRUGO, pod_get_band_5_frequency, pod_set_band_5_frequency);
static DEVICE_ATTR(tuner, S_IWUGO | S_IRUGO, pod_get_tuner, pod_set_tuner);
static DEVICE_ATTR(mic_selection, S_IWUGO | S_IRUGO, pod_get_mic_selection, pod_set_mic_selection);
static DEVICE_ATTR(cabinet_model, S_IWUGO | S_IRUGO, pod_get_cabinet_model, pod_set_cabinet_model);
static DEVICE_ATTR(stomp_model, S_IWUGO | S_IRUGO, pod_get_stomp_model, pod_set_stomp_model);
static DEVICE_ATTR(roomlevel, S_IWUGO | S_IRUGO, pod_get_roomlevel, pod_set_roomlevel);
static DEVICE_ATTR(band_4_frequency, S_IWUGO | S_IRUGO, pod_get_band_4_frequency, pod_set_band_4_frequency);
static DEVICE_ATTR(band_6_frequency, S_IWUGO | S_IRUGO, pod_get_band_6_frequency, pod_set_band_6_frequency);
static DEVICE_ATTR(stomp_param_1_note_value, S_IWUGO | S_IRUGO, pod_get_stomp_param_1_note_value, pod_set_stomp_param_1_note_value);
static DEVICE_ATTR(stomp_param_2, S_IWUGO | S_IRUGO, pod_get_stomp_param_2, pod_set_stomp_param_2);
static DEVICE_ATTR(stomp_param_3, S_IWUGO | S_IRUGO, pod_get_stomp_param_3, pod_set_stomp_param_3);
static DEVICE_ATTR(stomp_param_4, S_IWUGO | S_IRUGO, pod_get_stomp_param_4, pod_set_stomp_param_4);
static DEVICE_ATTR(stomp_param_5, S_IWUGO | S_IRUGO, pod_get_stomp_param_5, pod_set_stomp_param_5);
static DEVICE_ATTR(stomp_param_6, S_IWUGO | S_IRUGO, pod_get_stomp_param_6, pod_set_stomp_param_6);
static DEVICE_ATTR(amp_switch_select, S_IWUGO | S_IRUGO, pod_get_amp_switch_select, pod_set_amp_switch_select);
static DEVICE_ATTR(delay_param_4, S_IWUGO | S_IRUGO, pod_get_delay_param_4, pod_set_delay_param_4);
static DEVICE_ATTR(delay_param_5, S_IWUGO | S_IRUGO, pod_get_delay_param_5, pod_set_delay_param_5);
static DEVICE_ATTR(delay_pre_post, S_IWUGO | S_IRUGO, pod_get_delay_pre_post, pod_set_delay_pre_post);
static DEVICE_ATTR(delay_model, S_IWUGO | S_IRUGO, pod_get_delay_model, pod_set_delay_model);
static DEVICE_ATTR(delay_verb_model, S_IWUGO | S_IRUGO, pod_get_delay_verb_model, pod_set_delay_verb_model);
static DEVICE_ATTR(tempo_msb, S_IWUGO | S_IRUGO, pod_get_tempo_msb, pod_set_tempo_msb);
static DEVICE_ATTR(tempo_lsb, S_IWUGO | S_IRUGO, pod_get_tempo_lsb, pod_set_tempo_lsb);
static DEVICE_ATTR(wah_model, S_IWUGO | S_IRUGO, pod_get_wah_model, pod_set_wah_model);
static DEVICE_ATTR(bypass_volume, S_IWUGO | S_IRUGO, pod_get_bypass_volume, pod_set_bypass_volume);
static DEVICE_ATTR(fx_loop_on_off, S_IWUGO | S_IRUGO, pod_get_fx_loop_on_off, pod_set_fx_loop_on_off);
static DEVICE_ATTR(tweak_param_select, S_IWUGO | S_IRUGO, pod_get_tweak_param_select, pod_set_tweak_param_select);
static DEVICE_ATTR(amp1_engage, S_IWUGO | S_IRUGO, pod_get_amp1_engage, pod_set_amp1_engage);
static DEVICE_ATTR(band_1_gain, S_IWUGO | S_IRUGO, pod_get_band_1_gain, pod_set_band_1_gain);
static DEVICE_ATTR2(band_2_gain__bass, band_2_gain, S_IWUGO | S_IRUGO, pod_get_band_2_gain__bass, pod_set_band_2_gain__bass);
static DEVICE_ATTR(band_2_gain, S_IWUGO | S_IRUGO, pod_get_band_2_gain, pod_set_band_2_gain);
static DEVICE_ATTR2(band_3_gain__bass, band_3_gain, S_IWUGO | S_IRUGO, pod_get_band_3_gain__bass, pod_set_band_3_gain__bass);
static DEVICE_ATTR(band_3_gain, S_IWUGO | S_IRUGO, pod_get_band_3_gain, pod_set_band_3_gain);
static DEVICE_ATTR2(band_4_gain__bass, band_4_gain, S_IWUGO | S_IRUGO, pod_get_band_4_gain__bass, pod_set_band_4_gain__bass);
static DEVICE_ATTR2(band_5_gain__bass, band_5_gain, S_IWUGO | S_IRUGO, pod_get_band_5_gain__bass, pod_set_band_5_gain__bass);
static DEVICE_ATTR(band_4_gain, S_IWUGO | S_IRUGO, pod_get_band_4_gain, pod_set_band_4_gain);
static DEVICE_ATTR2(band_6_gain__bass, band_6_gain, S_IWUGO | S_IRUGO, pod_get_band_6_gain__bass, pod_set_band_6_gain__bass);
static DEVICE_ATTR(body, S_IRUGO, variax_get_body, line6_nop_write);
static DEVICE_ATTR(pickup1_enable, S_IRUGO, variax_get_pickup1_enable, line6_nop_write);
static DEVICE_ATTR(pickup1_type, S_IRUGO, variax_get_pickup1_type, line6_nop_write);
static DEVICE_ATTR(pickup1_position, S_IRUGO, variax_get_pickup1_position, line6_nop_write);
static DEVICE_ATTR(pickup1_angle, S_IRUGO, variax_get_pickup1_angle, line6_nop_write);
static DEVICE_ATTR(pickup1_level, S_IRUGO, variax_get_pickup1_level, line6_nop_write);
static DEVICE_ATTR(pickup2_enable, S_IRUGO, variax_get_pickup2_enable, line6_nop_write);
static DEVICE_ATTR(pickup2_type, S_IRUGO, variax_get_pickup2_type, line6_nop_write);
static DEVICE_ATTR(pickup2_position, S_IRUGO, variax_get_pickup2_position, line6_nop_write);
static DEVICE_ATTR(pickup2_angle, S_IRUGO, variax_get_pickup2_angle, line6_nop_write);
static DEVICE_ATTR(pickup2_level, S_IRUGO, variax_get_pickup2_level, line6_nop_write);
static DEVICE_ATTR(pickup_phase, S_IRUGO, variax_get_pickup_phase, line6_nop_write);
static DEVICE_ATTR(capacitance, S_IRUGO, variax_get_capacitance, line6_nop_write);
static DEVICE_ATTR(tone_resistance, S_IRUGO, variax_get_tone_resistance, line6_nop_write);
static DEVICE_ATTR(volume_resistance, S_IRUGO, variax_get_volume_resistance, line6_nop_write);
static DEVICE_ATTR(taper, S_IRUGO, variax_get_taper, line6_nop_write);
static DEVICE_ATTR(tone_dump, S_IRUGO, variax_get_tone_dump, line6_nop_write);
static DEVICE_ATTR(save_tone, S_IRUGO, variax_get_save_tone, line6_nop_write);
static DEVICE_ATTR(volume_dump, S_IRUGO, variax_get_volume_dump, line6_nop_write);
static DEVICE_ATTR(tuning_enable, S_IRUGO, variax_get_tuning_enable, line6_nop_write);
static DEVICE_ATTR(tuning6, S_IRUGO, variax_get_tuning6, line6_nop_write);
static DEVICE_ATTR(tuning5, S_IRUGO, variax_get_tuning5, line6_nop_write);
static DEVICE_ATTR(tuning4, S_IRUGO, variax_get_tuning4, line6_nop_write);
static DEVICE_ATTR(tuning3, S_IRUGO, variax_get_tuning3, line6_nop_write);
static DEVICE_ATTR(tuning2, S_IRUGO, variax_get_tuning2, line6_nop_write);
static DEVICE_ATTR(tuning1, S_IRUGO, variax_get_tuning1, line6_nop_write);
static DEVICE_ATTR(detune6, S_IRUGO, variax_get_detune6, line6_nop_write);
static DEVICE_ATTR(detune5, S_IRUGO, variax_get_detune5, line6_nop_write);
static DEVICE_ATTR(detune4, S_IRUGO, variax_get_detune4, line6_nop_write);
static DEVICE_ATTR(detune3, S_IRUGO, variax_get_detune3, line6_nop_write);
static DEVICE_ATTR(detune2, S_IRUGO, variax_get_detune2, line6_nop_write);
static DEVICE_ATTR(detune1, S_IRUGO, variax_get_detune1, line6_nop_write);
static DEVICE_ATTR(mix6, S_IRUGO, variax_get_mix6, line6_nop_write);
static DEVICE_ATTR(mix5, S_IRUGO, variax_get_mix5, line6_nop_write);
static DEVICE_ATTR(mix4, S_IRUGO, variax_get_mix4, line6_nop_write);
static DEVICE_ATTR(mix3, S_IRUGO, variax_get_mix3, line6_nop_write);
static DEVICE_ATTR(mix2, S_IRUGO, variax_get_mix2, line6_nop_write);
static DEVICE_ATTR(mix1, S_IRUGO, variax_get_mix1, line6_nop_write);
static DEVICE_ATTR(pickup_wiring, S_IRUGO, variax_get_pickup_wiring, line6_nop_write);

int pod_create_files(int firmware, int type, struct device *dev)
{
	int err;
	CHECK_RETURN(device_create_file(dev, &dev_attr_tweak));
	CHECK_RETURN(device_create_file(dev, &dev_attr_wah_position));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_compression_gain));
	CHECK_RETURN(device_create_file(dev, &dev_attr_vol_pedal_position));
	CHECK_RETURN(device_create_file(dev, &dev_attr_compression_threshold));
	CHECK_RETURN(device_create_file(dev, &dev_attr_pan));
	CHECK_RETURN(device_create_file(dev, &dev_attr_amp_model_setup));
	if (firmware >= 200)
		CHECK_RETURN(device_create_file(dev, &dev_attr_amp_model));
	CHECK_RETURN(device_create_file(dev, &dev_attr_drive));
	CHECK_RETURN(device_create_file(dev, &dev_attr_bass));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_mid));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_lowmid));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_treble));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_highmid));
	CHECK_RETURN(device_create_file(dev, &dev_attr_chan_vol));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_reverb_mix));
	CHECK_RETURN(device_create_file(dev, &dev_attr_effect_setup));
	if (firmware >= 200)
		CHECK_RETURN(device_create_file(dev, &dev_attr_band_1_frequency));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_presence));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_treble__bass));
	CHECK_RETURN(device_create_file(dev, &dev_attr_noise_gate_enable));
	CHECK_RETURN(device_create_file(dev, &dev_attr_gate_threshold));
	CHECK_RETURN(device_create_file(dev, &dev_attr_gate_decay_time));
	CHECK_RETURN(device_create_file(dev, &dev_attr_stomp_enable));
	CHECK_RETURN(device_create_file(dev, &dev_attr_comp_enable));
	CHECK_RETURN(device_create_file(dev, &dev_attr_stomp_time));
	CHECK_RETURN(device_create_file(dev, &dev_attr_delay_enable));
	CHECK_RETURN(device_create_file(dev, &dev_attr_mod_param_1));
	CHECK_RETURN(device_create_file(dev, &dev_attr_delay_param_1));
	CHECK_RETURN(device_create_file(dev, &dev_attr_delay_param_1_note_value));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_band_2_frequency__bass));
	CHECK_RETURN(device_create_file(dev, &dev_attr_delay_param_2));
	CHECK_RETURN(device_create_file(dev, &dev_attr_delay_volume_mix));
	CHECK_RETURN(device_create_file(dev, &dev_attr_delay_param_3));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_reverb_enable));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_reverb_type));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_reverb_decay));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_reverb_tone));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_reverb_pre_delay));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_reverb_pre_post));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_band_2_frequency));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_band_3_frequency__bass));
	CHECK_RETURN(device_create_file(dev, &dev_attr_wah_enable));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_modulation_lo_cut));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_delay_reverb_lo_cut));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_volume_pedal_minimum));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_eq_pre_post));
	CHECK_RETURN(device_create_file(dev, &dev_attr_volume_pre_post));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_di_model));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_di_delay));
	CHECK_RETURN(device_create_file(dev, &dev_attr_mod_enable));
	CHECK_RETURN(device_create_file(dev, &dev_attr_mod_param_1_note_value));
	CHECK_RETURN(device_create_file(dev, &dev_attr_mod_param_2));
	CHECK_RETURN(device_create_file(dev, &dev_attr_mod_param_3));
	CHECK_RETURN(device_create_file(dev, &dev_attr_mod_param_4));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_mod_param_5));
	CHECK_RETURN(device_create_file(dev, &dev_attr_mod_volume_mix));
	CHECK_RETURN(device_create_file(dev, &dev_attr_mod_pre_post));
	CHECK_RETURN(device_create_file(dev, &dev_attr_modulation_model));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_band_3_frequency));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_band_4_frequency__bass));
	CHECK_RETURN(device_create_file(dev, &dev_attr_mod_param_1_double_precision));
	CHECK_RETURN(device_create_file(dev, &dev_attr_delay_param_1_double_precision));
	if (firmware >= 200)
		CHECK_RETURN(device_create_file(dev, &dev_attr_eq_enable));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tap));
	CHECK_RETURN(device_create_file(dev, &dev_attr_volume_tweak_pedal_assign));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_band_5_frequency));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tuner));
	CHECK_RETURN(device_create_file(dev, &dev_attr_mic_selection));
	CHECK_RETURN(device_create_file(dev, &dev_attr_cabinet_model));
	CHECK_RETURN(device_create_file(dev, &dev_attr_stomp_model));
	CHECK_RETURN(device_create_file(dev, &dev_attr_roomlevel));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_band_4_frequency));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_band_6_frequency));
	CHECK_RETURN(device_create_file(dev, &dev_attr_stomp_param_1_note_value));
	CHECK_RETURN(device_create_file(dev, &dev_attr_stomp_param_2));
	CHECK_RETURN(device_create_file(dev, &dev_attr_stomp_param_3));
	CHECK_RETURN(device_create_file(dev, &dev_attr_stomp_param_4));
	CHECK_RETURN(device_create_file(dev, &dev_attr_stomp_param_5));
	CHECK_RETURN(device_create_file(dev, &dev_attr_stomp_param_6));
	if ((type & (LINE6_BITS_LIVE)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_amp_switch_select));
	CHECK_RETURN(device_create_file(dev, &dev_attr_delay_param_4));
	CHECK_RETURN(device_create_file(dev, &dev_attr_delay_param_5));
	CHECK_RETURN(device_create_file(dev, &dev_attr_delay_pre_post));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_delay_model));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_delay_verb_model));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tempo_msb));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tempo_lsb));
	if (firmware >= 300)
		CHECK_RETURN(device_create_file(dev, &dev_attr_wah_model));
	if (firmware >= 214)
		CHECK_RETURN(device_create_file(dev, &dev_attr_bypass_volume));
	if ((type & (LINE6_BITS_PRO)) != 0)
		CHECK_RETURN(device_create_file(dev, &dev_attr_fx_loop_on_off));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tweak_param_select));
	CHECK_RETURN(device_create_file(dev, &dev_attr_amp1_engage));
	if (firmware >= 200)
		CHECK_RETURN(device_create_file(dev, &dev_attr_band_1_gain));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_band_2_gain__bass));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_band_2_gain));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_band_3_gain__bass));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_band_3_gain));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_band_4_gain__bass));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_band_5_gain__bass));
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_band_4_gain));
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			CHECK_RETURN(device_create_file(dev, &dev_attr_band_6_gain__bass));
  return 0;
}

void pod_remove_files(int firmware, int type, struct device *dev)
{
	device_remove_file(dev, &dev_attr_tweak);
	device_remove_file(dev, &dev_attr_wah_position);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_compression_gain);
	device_remove_file(dev, &dev_attr_vol_pedal_position);
	device_remove_file(dev, &dev_attr_compression_threshold);
	device_remove_file(dev, &dev_attr_pan);
	device_remove_file(dev, &dev_attr_amp_model_setup);
	if (firmware >= 200)
		device_remove_file(dev, &dev_attr_amp_model);
	device_remove_file(dev, &dev_attr_drive);
	device_remove_file(dev, &dev_attr_bass);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_mid);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_lowmid);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_treble);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_highmid);
	device_remove_file(dev, &dev_attr_chan_vol);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_reverb_mix);
	device_remove_file(dev, &dev_attr_effect_setup);
	if (firmware >= 200)
		device_remove_file(dev, &dev_attr_band_1_frequency);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_presence);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_treble__bass);
	device_remove_file(dev, &dev_attr_noise_gate_enable);
	device_remove_file(dev, &dev_attr_gate_threshold);
	device_remove_file(dev, &dev_attr_gate_decay_time);
	device_remove_file(dev, &dev_attr_stomp_enable);
	device_remove_file(dev, &dev_attr_comp_enable);
	device_remove_file(dev, &dev_attr_stomp_time);
	device_remove_file(dev, &dev_attr_delay_enable);
	device_remove_file(dev, &dev_attr_mod_param_1);
	device_remove_file(dev, &dev_attr_delay_param_1);
	device_remove_file(dev, &dev_attr_delay_param_1_note_value);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_band_2_frequency__bass);
	device_remove_file(dev, &dev_attr_delay_param_2);
	device_remove_file(dev, &dev_attr_delay_volume_mix);
	device_remove_file(dev, &dev_attr_delay_param_3);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_reverb_enable);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_reverb_type);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_reverb_decay);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_reverb_tone);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_reverb_pre_delay);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_reverb_pre_post);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_band_2_frequency);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_band_3_frequency__bass);
	device_remove_file(dev, &dev_attr_wah_enable);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_modulation_lo_cut);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_delay_reverb_lo_cut);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_volume_pedal_minimum);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_eq_pre_post);
	device_remove_file(dev, &dev_attr_volume_pre_post);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_di_model);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_di_delay);
	device_remove_file(dev, &dev_attr_mod_enable);
	device_remove_file(dev, &dev_attr_mod_param_1_note_value);
	device_remove_file(dev, &dev_attr_mod_param_2);
	device_remove_file(dev, &dev_attr_mod_param_3);
	device_remove_file(dev, &dev_attr_mod_param_4);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_mod_param_5);
	device_remove_file(dev, &dev_attr_mod_volume_mix);
	device_remove_file(dev, &dev_attr_mod_pre_post);
	device_remove_file(dev, &dev_attr_modulation_model);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_band_3_frequency);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_band_4_frequency__bass);
	device_remove_file(dev, &dev_attr_mod_param_1_double_precision);
	device_remove_file(dev, &dev_attr_delay_param_1_double_precision);
	if (firmware >= 200)
		device_remove_file(dev, &dev_attr_eq_enable);
	device_remove_file(dev, &dev_attr_tap);
	device_remove_file(dev, &dev_attr_volume_tweak_pedal_assign);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_band_5_frequency);
	device_remove_file(dev, &dev_attr_tuner);
	device_remove_file(dev, &dev_attr_mic_selection);
	device_remove_file(dev, &dev_attr_cabinet_model);
	device_remove_file(dev, &dev_attr_stomp_model);
	device_remove_file(dev, &dev_attr_roomlevel);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_band_4_frequency);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_band_6_frequency);
	device_remove_file(dev, &dev_attr_stomp_param_1_note_value);
	device_remove_file(dev, &dev_attr_stomp_param_2);
	device_remove_file(dev, &dev_attr_stomp_param_3);
	device_remove_file(dev, &dev_attr_stomp_param_4);
	device_remove_file(dev, &dev_attr_stomp_param_5);
	device_remove_file(dev, &dev_attr_stomp_param_6);
	if ((type & (LINE6_BITS_LIVE)) != 0)
		device_remove_file(dev, &dev_attr_amp_switch_select);
	device_remove_file(dev, &dev_attr_delay_param_4);
	device_remove_file(dev, &dev_attr_delay_param_5);
	device_remove_file(dev, &dev_attr_delay_pre_post);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_delay_model);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		device_remove_file(dev, &dev_attr_delay_verb_model);
	device_remove_file(dev, &dev_attr_tempo_msb);
	device_remove_file(dev, &dev_attr_tempo_lsb);
	if (firmware >= 300)
		device_remove_file(dev, &dev_attr_wah_model);
	if (firmware >= 214)
		device_remove_file(dev, &dev_attr_bypass_volume);
	if ((type & (LINE6_BITS_PRO)) != 0)
		device_remove_file(dev, &dev_attr_fx_loop_on_off);
	device_remove_file(dev, &dev_attr_tweak_param_select);
	device_remove_file(dev, &dev_attr_amp1_engage);
	if (firmware >= 200)
		device_remove_file(dev, &dev_attr_band_1_gain);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_band_2_gain__bass);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_band_2_gain);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_band_3_gain__bass);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_band_3_gain);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_band_4_gain__bass);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_band_5_gain__bass);
	if ((type & (LINE6_BITS_PODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_band_4_gain);
	if ((type & (LINE6_BITS_BASSPODXTALL)) != 0)
		if (firmware >= 200)
			device_remove_file(dev, &dev_attr_band_6_gain__bass);
}

EXPORT_SYMBOL(pod_create_files);
EXPORT_SYMBOL(pod_remove_files);

int variax_create_files(int firmware, int type, struct device *dev)
{
	int err;
	CHECK_RETURN(device_create_file(dev, &dev_attr_body));
	CHECK_RETURN(device_create_file(dev, &dev_attr_pickup1_enable));
	CHECK_RETURN(device_create_file(dev, &dev_attr_pickup1_type));
	CHECK_RETURN(device_create_file(dev, &dev_attr_pickup1_position));
	CHECK_RETURN(device_create_file(dev, &dev_attr_pickup1_angle));
	CHECK_RETURN(device_create_file(dev, &dev_attr_pickup1_level));
	CHECK_RETURN(device_create_file(dev, &dev_attr_pickup2_enable));
	CHECK_RETURN(device_create_file(dev, &dev_attr_pickup2_type));
	CHECK_RETURN(device_create_file(dev, &dev_attr_pickup2_position));
	CHECK_RETURN(device_create_file(dev, &dev_attr_pickup2_angle));
	CHECK_RETURN(device_create_file(dev, &dev_attr_pickup2_level));
	CHECK_RETURN(device_create_file(dev, &dev_attr_pickup_phase));
	CHECK_RETURN(device_create_file(dev, &dev_attr_capacitance));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tone_resistance));
	CHECK_RETURN(device_create_file(dev, &dev_attr_volume_resistance));
	CHECK_RETURN(device_create_file(dev, &dev_attr_taper));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tone_dump));
	CHECK_RETURN(device_create_file(dev, &dev_attr_save_tone));
	CHECK_RETURN(device_create_file(dev, &dev_attr_volume_dump));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tuning_enable));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tuning6));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tuning5));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tuning4));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tuning3));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tuning2));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tuning1));
	CHECK_RETURN(device_create_file(dev, &dev_attr_detune6));
	CHECK_RETURN(device_create_file(dev, &dev_attr_detune5));
	CHECK_RETURN(device_create_file(dev, &dev_attr_detune4));
	CHECK_RETURN(device_create_file(dev, &dev_attr_detune3));
	CHECK_RETURN(device_create_file(dev, &dev_attr_detune2));
	CHECK_RETURN(device_create_file(dev, &dev_attr_detune1));
	CHECK_RETURN(device_create_file(dev, &dev_attr_mix6));
	CHECK_RETURN(device_create_file(dev, &dev_attr_mix5));
	CHECK_RETURN(device_create_file(dev, &dev_attr_mix4));
	CHECK_RETURN(device_create_file(dev, &dev_attr_mix3));
	CHECK_RETURN(device_create_file(dev, &dev_attr_mix2));
	CHECK_RETURN(device_create_file(dev, &dev_attr_mix1));
	CHECK_RETURN(device_create_file(dev, &dev_attr_pickup_wiring));
  return 0;
}

void variax_remove_files(int firmware, int type, struct device *dev)
{
	device_remove_file(dev, &dev_attr_body);
	device_remove_file(dev, &dev_attr_pickup1_enable);
	device_remove_file(dev, &dev_attr_pickup1_type);
	device_remove_file(dev, &dev_attr_pickup1_position);
	device_remove_file(dev, &dev_attr_pickup1_angle);
	device_remove_file(dev, &dev_attr_pickup1_level);
	device_remove_file(dev, &dev_attr_pickup2_enable);
	device_remove_file(dev, &dev_attr_pickup2_type);
	device_remove_file(dev, &dev_attr_pickup2_position);
	device_remove_file(dev, &dev_attr_pickup2_angle);
	device_remove_file(dev, &dev_attr_pickup2_level);
	device_remove_file(dev, &dev_attr_pickup_phase);
	device_remove_file(dev, &dev_attr_capacitance);
	device_remove_file(dev, &dev_attr_tone_resistance);
	device_remove_file(dev, &dev_attr_volume_resistance);
	device_remove_file(dev, &dev_attr_taper);
	device_remove_file(dev, &dev_attr_tone_dump);
	device_remove_file(dev, &dev_attr_save_tone);
	device_remove_file(dev, &dev_attr_volume_dump);
	device_remove_file(dev, &dev_attr_tuning_enable);
	device_remove_file(dev, &dev_attr_tuning6);
	device_remove_file(dev, &dev_attr_tuning5);
	device_remove_file(dev, &dev_attr_tuning4);
	device_remove_file(dev, &dev_attr_tuning3);
	device_remove_file(dev, &dev_attr_tuning2);
	device_remove_file(dev, &dev_attr_tuning1);
	device_remove_file(dev, &dev_attr_detune6);
	device_remove_file(dev, &dev_attr_detune5);
	device_remove_file(dev, &dev_attr_detune4);
	device_remove_file(dev, &dev_attr_detune3);
	device_remove_file(dev, &dev_attr_detune2);
	device_remove_file(dev, &dev_attr_detune1);
	device_remove_file(dev, &dev_attr_mix6);
	device_remove_file(dev, &dev_attr_mix5);
	device_remove_file(dev, &dev_attr_mix4);
	device_remove_file(dev, &dev_attr_mix3);
	device_remove_file(dev, &dev_attr_mix2);
	device_remove_file(dev, &dev_attr_mix1);
	device_remove_file(dev, &dev_attr_pickup_wiring);
}

EXPORT_SYMBOL(variax_create_files);
EXPORT_SYMBOL(variax_remove_files);
