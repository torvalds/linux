/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RK_HEADSET_H
#define RK_HEADSET_H

#define HEADSET_IN_HIGH 0x00000001
#define HEADSET_IN_LOW  0x00000000

#define HOOK_DOWN_HIGH 0x00000001
#define HOOK_DOWN_LOW  0x00000000

struct rk_headset_pdata{
//heaset about
	unsigned int headset_gpio;
	unsigned int headset_insert_type;//	Headphones into the state level
//hook about
	unsigned int hook_gpio;
	unsigned int hook_down_type; //Hook key down status  
#ifdef CONFIG_MODEM_MIC_SWITCH
//mic about	
	unsigned int mic_switch_gpio;
	unsigned int hp_mic_io_value;
	unsigned int main_mic_io_value;	
#endif
	struct iio_channel *chan;
	int headset_wakeup;
};

#define HOOK_KEY_CODE KEY_MEDIA

extern int rk_headset_probe(struct platform_device *pdev,struct rk_headset_pdata *pdata);
extern int rk_headset_adc_probe(struct platform_device *pdev,struct rk_headset_pdata *pdata);
extern int rk_headset_adc_suspend(struct platform_device *pdev, pm_message_t state);
extern int rk_headset_adc_resume(struct platform_device *pdev);
#endif
